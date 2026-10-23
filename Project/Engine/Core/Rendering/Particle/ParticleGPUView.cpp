#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>

using namespace Engine;

ParticleGPUDrawData ParticleGPUService::PrepareView(GraphicsCore& graphics,
	const std::shared_ptr<ParticleGPUState>& state, RenderAssetLibrary& assets,
	PipelineStateCache& pipelines, const std::string& viewKey, ParticleGPUViewConstants constants) {

	const auto found = instances_.find(state.get());
	if (found == instances_.end() || state->rejected) return {};
	if (state->statisticsValid && state->completedSequence == state->lastSubmittedSequence &&
		state->statistics.particleCount == 0 && state->statistics.trailCount == 0) return {};
	auto& instance = *found->second;
	auto& view = instance.views[viewKey];
	if (!view) {
		auto candidate = std::make_unique<ParticleGPUViewResources>();
		candidate->Init(graphics, constants.indexCount ? instance.capacity : 1u,
			instance.phaseCount, instance.trailCapacity * instance.pointCapacity);
		view = std::move(candidate);
	}
	view->lastUsedFrame = GraphicsFrameState::GetFrameSerial();
	const auto result = [&]() {
		ParticleGPUDrawData data{view->arguments.GetResource(), instance.particles.GetGPUAddress(),
			instance.parents.GetGPUAddress(), view->keys[0].GetGPUAddress(), view->counts.GetGPUAddress()};
		if (instance.trailCapacity) {
			data.trailArguments = view->trailArguments.GetResource();
			data.trails = instance.trails.GetGPUAddress();
			data.trailPoints = instance.trailPoints.GetGPUAddress();
			data.trailSegments = view->trailKeys[0].GetGPUAddress();
			data.ribbonOrder = instance.ribbonOrder.GetGPUAddress();
		}
		return data;
	};
	if (view->preparedFrame == view->lastUsedFrame && view->preparedEpoch == state->epoch &&
		view->preparedSequence == state->lastSubmittedSequence) return result();
	auto& platform = graphics.GetDXObject();
	const auto* pipeline = pipelines.GetORCreate(platform, assets, BuiltinAssets::Pipelines::ParticleGPUView,
		PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipeline || !pipeline->GetComputePipeline()) return {};
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	// 元粒子を描かないTrailでは粒子数分のソートを省く
	if (constants.indexCount) {
		for (auto& buffer : view->keys) buffer.EnsureCapacity(instance.capacity);
		const uint32_t sortCapacity = (std::max)(instance.capacity, instance.trailCapacity * instance.pointCapacity);
		view->histogram.EnsureCapacity(((sortCapacity + 255) / 256) * 16);
		view->histogramOffsets.EnsureCapacity(((sortCapacity + 255) / 256) * 16);
	}
	view->histogram.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	view->histogramOffsets.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	constants.capacity = instance.capacity;
	constants.phaseCount = instance.phaseCount;
	constants.groupCount = (instance.capacity + 255) / 256;
	uint32_t input = 1;
	uint32_t output = 0;
	const auto dispatch = [&](uint32_t operation, uint32_t groups) {
		view->keys[input].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		view->keys[output].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		constants.operation = operation;
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
		BindParticleGPUViewCompute(list, *pipeline, allocation.gpuAddress, instance, *view, input, output, false);
		list->Dispatch(groups, 1, 1);
		commands.UAVBarrierAll();
	};
	if (constants.indexCount) {
		for (auto& buffer : view->keys) buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		view->counts.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		view->arguments.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		// 可視粒子数とPhaseごとの開始位置を確定する
		dispatch(0, ((std::max)(instance.capacity, instance.phaseCount * 2 + 4) + 255) / 256);
		dispatch(1, constants.groupCount);
		dispatch(2, 1);
		if (constants.sortEnabled || instance.phaseCount > 1) {
			// Phase順を上位桁、Cameraからの奥行きを下位桁で安定ソートする
			for (uint32_t shift = constants.sortEnabled ? 0u : 24u; shift < 32; shift += 4) {
				std::swap(input, output);
				constants.radixShift = shift;
				dispatch(3, constants.groupCount);
				dispatch(4, 1);
				dispatch(5, constants.groupCount);
			}
		}
		dispatch(6, (instance.phaseCount + 255) / 256);
		view->keys[0].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		view->counts.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		view->arguments.Transition(commands, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	}
	if (instance.trailCapacity) {
		const auto* trailPipeline = pipelines.GetORCreate(platform, assets, BuiltinAssets::Pipelines::ParticleGPUTrailPrepare,
			PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
		if (!trailPipeline) return {};
		PrepareTrailView(graphics, instance, *view, *trailPipeline, *pipeline, constants);
	}
	view->preparedFrame = view->lastUsedFrame;
	view->preparedEpoch = state->epoch;
	view->preparedSequence = state->lastSubmittedSequence;
	return result();
}
