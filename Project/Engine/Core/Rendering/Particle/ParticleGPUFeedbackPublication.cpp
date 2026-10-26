#include "ParticleGPUFeedbackWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>

// c++
#include <algorithm>

using namespace Engine;

void ParticleGPUFeedbackWorld::RefreshPaint(GraphicsCore& graphics, ECSWorld& world, const PipelineState& pipeline) {

	PrepareDefaults(graphics);
	if (constantFrameSerial_ != GraphicsFrameState::GetFrameSerial()) {
		constants_.BeginFrame();
		constantFrameSerial_ = GraphicsFrameState::GetFrameSerial();
	}
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	events_[writeIndex_].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	eventCounts_[writeIndex_].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	counters_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const uint32_t capacity = eventCapacity_;
	eventCapacity_ = 0;
	try { UpdatePaint(graphics, world, pipeline); }
	catch (...) { eventCapacity_ = capacity; throw; }
	eventCapacity_ = capacity;
}

void ParticleGPUFeedbackWorld::EndUpdate(GraphicsCore& graphics, ECSWorld& world, RenderAssetLibrary& assets,
	const PipelineState& paintPipeline, const PipelineState& decalPipeline, float deltaTime) {

	if (!eventsInitialized_) return;
	auto& commands = *graphics.GetDXObject().GetDxCommand();
	commands.UAVBarrierAll();
	UpdatePaint(graphics, world, paintPipeline);
	UpdateDecals(graphics, world, assets, decalPipeline, deltaTime);
	++surfaceSequence_;
	if (decalEmissionPossible_) {
		decalEmissionSequence_ = surfaceSequence_;
		decalEmissionSubmission_ = commands.GetSubmissionTicket();
	}
	decalEmissionPossible_ = false;
	for (auto& readback : readbacks_) {
		if (readback.submission) continue;
		counters_.Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands.GetCommandList()->CopyBufferRegion(readback.buffer.GetResource(), 0, counters_.GetResource(), 0, sizeof(statistics_));
		counters_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		readback.submission = commands.GetSubmissionTicket();
		readback.sequence = surfaceSequence_;
		break;
	}
}

void ParticleGPUFeedbackWorld::CollectStatistics(GraphicsCore& graphics) {

	const uint64_t completed = graphics.GetDXObject().GetCommandQueue()->GetCompletedFenceValue();
	for (auto& readback : readbacks_) {
		if (!readback.submission) continue;
		const uint64_t fence = readback.submission->GetFenceValue();
		if (fence == 0 || completed < fence) continue;
		if (readback.sequence >= statisticsSequence_) {
			statistics_ = readback.buffer.GetReadbackData();
			statisticsValid_ = true;
			statisticsFence_ = fence;
			statisticsSequence_ = readback.sequence;
		}
		readback.submission.reset();
	}
	statisticsDiagnostic_.clear();
	if (statisticsValid_ && (statistics_.eventOverflow || statistics_.childOverflow || statistics_.decalOverflow ||
		statistics_.paintOverflow || statistics_.invalidReceiver)) {
		statisticsDiagnostic_ = "World集計:イベント超過=" + std::to_string(statistics_.eventOverflow) +
			"、子粒子超過=" + std::to_string(statistics_.childOverflow) + "、デカール超過=" + std::to_string(statistics_.decalOverflow) +
			"、塗装Tile超過=" + std::to_string(statistics_.paintOverflow) + "、受け取り先またはUVなし=" + std::to_string(statistics_.invalidReceiver);
	}
}

void ParticleGPUFeedbackWorld::Publish(GraphicsCore& graphics, RenderBufferRegistry& registry) {

	PrepareDefaults(graphics);
	if (eventsInitialized_) CollectStatistics(graphics);
	if (constantFrameSerial_ != GraphicsFrameState::GetFrameSerial()) {
		constants_.BeginFrame();
		constantFrameSerial_ = GraphicsFrameState::GetFrameSerial();
	}
	decals_.Transition(*graphics.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	std::vector<ParticlePaintReceiverGPU> receivers;
	for (const auto& [key, receiver] : receivers_) receivers.push_back(receiver->data);
	std::sort(receivers.begin(), receivers.end(), [](const auto& a, const auto& b) { return a.entity.index < b.entity.index; });
	receiverCount_ = static_cast<uint32_t>(receivers.size());
	if (receivers.empty()) receivers.emplace_back();
	paintReceivers_.Upload(receivers);
	const auto publish = [&](const char* name, const auto& buffer, uint32_t count, uint32_t stride) {
		registry.Register({.alias = name, .resource = buffer.GetResource(), .gpuAddress = buffer.GetGPUAddress(),
			.srvGPUHandle = buffer.GetSRVGPUHandle(), .elementCount = count, .stride = stride});
	};
	registry.Register({.alias = "gParticlePaintReceivers", .resource = paintReceivers_.GetResource(),
		.gpuAddress = paintReceivers_.GetGPUAddress(), .srvGPUHandle = paintReceivers_.GetGPUHandle(),
		.elementCount = receiverCount_, .stride = sizeof(ParticlePaintReceiverGPU)});
	publish("gGPUImpactDecals", decals_, decalCapacity_, sizeof(ParticleImpactDecalGPU));
	decalBins_.Transition(*graphics.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	decalBinWords_.Transition(*graphics.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	const uint32_t wordCount = (decalCapacity_ + 31) / 32;
	const uint32_t summaryCount = (wordCount + 31) / 32;
	publish("gGPUImpactDecalBins", decalBins_, 257 * wordCount, sizeof(uint32_t));
	publish("gGPUImpactDecalBinWords", decalBinWords_, 257 * summaryCount, sizeof(uint32_t));
	registry.Register({.alias = "gGPUImpactDecalArguments", .resource = decalArguments_.GetResource(),
		.elementCount = decalCapacity_ ? 1u : 0u, .stride = 16});
	auto& platform = graphics.GetDXObject();
	const uint32_t paintCounts[]{receiverCount_, 0, 0, 0};
	const auto paint = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), paintCounts);
	registry.Register({.alias = "ParticlePaintSceneConstants", .gpuAddress = paint.gpuAddress});
	const uint32_t decalCounts[]{decalCapacity_, wordCount, summaryCount, 0};
	const auto decal = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), decalCounts);
	registry.Register({.alias = "ParticleImpactDecalSceneConstants", .gpuAddress = decal.gpuAddress});
}
