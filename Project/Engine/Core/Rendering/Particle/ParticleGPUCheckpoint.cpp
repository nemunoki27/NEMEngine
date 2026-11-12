#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	constexpr uint64_t kMemoryBudget = 64ull * 1024 * 1024;

	template<typename Callback>
	void ForEachBuffer(ParticleGPUInstance& instance, const Callback& callback) {

		callback(instance.particles);
		callback(instance.moduleStates);
		callback(instance.statistics);
		if (!instance.trailCapacity) return;
		callback(instance.trails);
		callback(instance.trailPoints);
		callback(instance.trailFreeList);
		callback(instance.ribbonOrder);
	}
}

ParticleGPUCheckpointResources::~ParticleGPUCheckpointResources() {

	if (const auto checkpoint = owner.lock()) checkpoint->available = false;
	if (!retirement) return;
	retirement->ReservePending(buffers.size());
	for (auto& buffer : buffers) retirement->Retire(std::move(buffer));
	if (budget) budget->Release(memoryBytes, std::move(lastUse));
}

void ParticleGPUCheckpointBudget::Collect(uint64_t completedFence) {

	std::erase_if(retired, [&](const auto& entry) {
		const auto fence = entry.first->GetFenceValue();
		if (!fence || completedFence < fence) return false;
		allocated -= entry.second;
		return true;
	});
}

void ParticleGPUCheckpointBudget::Release(uint64_t bytes, GraphicsSubmissionTicket submission) {

	if (!submission) allocated -= bytes;
	else retired.emplace_back(std::move(submission), bytes);
}

void Engine::CaptureParticleGPUCheckpoint(GraphicsCore& graphics, ParticleGPUInstance& instance,
	const std::shared_ptr<ParticleGPUCheckpoint>& checkpoint) {

	auto& platform = graphics.GetDXObject();
	CaptureParticleGPUCheckpoint(platform.GetDevice(), *platform.GetDxCommand(), platform.GetResourceRetirement(),
		instance, checkpoint, platform.GetCommandQueue()->GetCompletedFenceValue());
}

void Engine::CaptureParticleGPUCheckpoint(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
	ParticleGPUInstance& instance, const std::shared_ptr<ParticleGPUCheckpoint>& checkpoint, uint64_t completedFence) {

	if (!checkpoint) return;
	std::erase_if(instance.checkpoints, [](const auto& entry) { return entry.second->owner.expired(); });
	if (instance.checkpoints.contains(checkpoint.get())) return;
	uint64_t used = 0, bytes = 0;
	for (const auto& [key, value] : instance.checkpoints) used += value->memoryBytes;
	ForEachBuffer(instance, [&](auto& buffer) { bytes += buffer.GetResource()->GetDesc().Width; });
	checkpoint->memoryBytes = bytes;
	if (bytes > kMemoryBudget || used > kMemoryBudget - bytes || instance.checkpoints.size() >= 9) return;
	if (!instance.checkpointBudget) instance.checkpointBudget = std::make_shared<ParticleGPUCheckpointBudget>();
	auto& budget = *instance.checkpointBudget;
	budget.Collect(completedFence);
	if (budget.allocated > ParticleGPUCheckpointBudget::kMemoryBudget - bytes) return;
	auto candidate = std::make_unique<ParticleGPUCheckpointResources>();
	candidate->owner = checkpoint;
	candidate->retirement = &retirement;
	candidate->memoryBytes = bytes;
	candidate->budget = instance.checkpointBudget;
	budget.allocated += bytes;
	candidate->emitterWorld = instance.lastEmitterWorld;
	ForEachBuffer(instance, [&](auto& buffer) {
		ComPtr<ID3D12Resource> copy;
		DxUtils::CreateDefaultBufferResource(device, copy, static_cast<size_t>(buffer.GetResource()->GetDesc().Width));
		candidate->buffers.push_back(std::move(copy));
	});
	candidate->lastUse = commands.GetSubmissionTicket();
	size_t index = 0;
	ForEachBuffer(instance, [&](auto& buffer) {
		auto* copy = candidate->buffers[index++].Get();
		buffer.Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands.TransitionBarriers({copy}, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		commands.GetCommandList()->CopyBufferRegion(copy, 0, buffer.GetResource(), 0, copy->GetDesc().Width);
		commands.TransitionBarriers({copy}, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
		buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	});
	instance.checkpoints.emplace(checkpoint.get(), std::move(candidate));
	checkpoint->available = true;
}

bool Engine::RestoreParticleGPUCheckpoint(GraphicsCore& graphics, ParticleGPUInstance& instance,
	const std::shared_ptr<const ParticleGPUCheckpoint>& checkpoint) {

	return RestoreParticleGPUCheckpoint(*graphics.GetDXObject().GetDxCommand(), instance, checkpoint);
}

bool Engine::RestoreParticleGPUCheckpoint(DxCommand& commands, ParticleGPUInstance& instance,
	const std::shared_ptr<const ParticleGPUCheckpoint>& checkpoint) {

	if (!checkpoint || !checkpoint->available) return false;
	const auto found = instance.checkpoints.find(checkpoint.get());
	if (found == instance.checkpoints.end()) return false;
	found->second->lastUse = commands.GetSubmissionTicket();
	size_t index = 0;
	ForEachBuffer(instance, [&](auto& buffer) {
		auto* copy = found->second->buffers[index++].Get();
		buffer.Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
		commands.GetCommandList()->CopyBufferRegion(buffer.GetResource(), 0, copy, 0, copy->GetDesc().Width);
		buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	});
	instance.lastEmitterWorld = found->second->emitterWorld;
	return true;
}
