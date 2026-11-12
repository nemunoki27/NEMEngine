#include "ParticleSimulationCheckpoint.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <unordered_map>

using namespace Engine;

namespace {

	void CopyInstance(const ParticleInstanceState& source, ParticleInstanceState& destination) {

		destination.randomGenerator = source.randomGenerator;
		destination.randomSeed = source.randomSeed;
		destination.randomInitialized = source.randomInitialized;
		destination.useAutoRandomSeed = source.useAutoRandomSeed;
		destination.oneShot = source.oneShot;
		destination.emissionStopped = source.emissionStopped;
		destination.target = source.target;
		destination.definition = source.definition;
		destination.rejectedSettings = source.rejectedSettings;
		destination.rejectedReason = source.rejectedReason;
		const auto& emitter = source.emitter;
		auto& result = destination.emitter;
		result.emitTimer = emitter.emitTimer;
		result.emitted = emitter.emitted;
		result.particles = emitter.particles;
		result.nextParticleID = emitter.nextParticleID;
		result.trailSerial = emitter.trailSerial;
		result.renderSettings = emitter.renderSettings;
		std::vector<std::pair<ParticleTrailHandle, ParticleTrailHandle>> handles;
		result.trails.CopyFrom(emitter.trails, handles);
		std::unordered_map<uint64_t, ParticleTrailHandle> replacements;
		replacements.reserve(handles.size());
		for (const auto& [old, replacement] : handles)
			replacements.emplace((uint64_t(old.generation) << 32) | old.index, replacement);
		const auto resolve = [&](ParticleTrailHandle handle) {
			if (!handle.IsValid()) return handle;
			const auto found = replacements.find((uint64_t(handle.generation) << 32) | handle.index);
			return found == replacements.end() ? ParticleTrailHandle::Null() : found->second;
		};
		result.ribbon = resolve(emitter.ribbon);
		for (size_t i = 0; i < result.particles.size(); ++i) {
			auto particle = result.particles.GetMutable(i);
			const auto handle = resolve({particle.trailIndex, particle.trailGeneration});
			particle.trailIndex = handle.index;
			particle.trailGeneration = handle.generation;
			result.particles.Set(i, particle);
		}
	}
}

ParticleSimulationCheckpoint::ParticleSimulationCheckpoint(ParticleInstanceState& instance) {

	CopyInstance(instance, state_);
	const auto& gpu = instance.emitter.gpu;
	if (!gpu || gpu->pending.empty()) return;
	gpuOwner_ = gpu;
	gpuCheckpoint_ = std::make_shared<ParticleGPUCheckpoint>();
	gpuParents_ = gpu->queuedParents;
	emissionEndSequence_ = gpu->emissionEndSequence;
	gpu->pending.back().captureCheckpoint = gpuCheckpoint_;
}

bool ParticleSimulationCheckpoint::CanRestore() const {

	return state_.target != ParticleSimulationTarget::GPU || (gpuCheckpoint_ && gpuCheckpoint_->available && !gpuOwner_.expired());
}

bool ParticleSimulationCheckpoint::Restore(ParticleInstanceState& instance) const {

	if (!CanRestore()) return false;
	auto gpu = instance.emitter.gpu;
	if (gpuCheckpoint_ && gpuOwner_.lock() != gpu) return false;
	CopyInstance(state_, instance);
	instance.emitter.gpu = gpu;
	if (!gpuCheckpoint_) return true;
	if (!gpu || !instance.definition || gpu->definition != instance.definition) return false;
	gpu->pending.clear();
	++gpu->epoch;
	gpu->statisticsValid = false;
	gpu->statistics = {};
	gpu->lastSubmittedSequence = gpu->completedSequence = 0;
	gpu->queuedParents = gpuParents_;
	ParticleGPUSimulationStep restore;
	restore.definition = gpu->definition;
	restore.program = gpu->program;
	restore.parents = gpuParents_;
	restore.sequence = ++gpu->nextSequence;
	restore.restoreCheckpoint = gpuCheckpoint_;
	gpu->emissionEndSequence = emissionEndSequence_ == UINT64_MAX ? UINT64_MAX : restore.sequence;
	gpu->pending.push_back(std::move(restore));
	return true;
}

uint64_t ParticleSimulationCheckpoint::GetMemoryBytes() const {

	return sizeof(*this) + state_.emitter.particles.GetSnapshotMemoryBytes() +
		state_.emitter.trails.GetSnapshotMemoryBytes() + gpuParents_.size() * sizeof(ParticleParentGPU) +
		(gpuCheckpoint_ ? gpuCheckpoint_->memoryBytes : 0);
}

void Engine::ResetParticleSimulation(ParticleInstanceState& instance, uint32_t seed, bool oneShot) {

	auto gpu = instance.emitter.gpu;
	const auto target = instance.target;
	const auto definition = instance.definition;
	instance = ParticleInstanceState{};
	instance.target = target;
	instance.definition = definition;
	instance.randomSeed = seed;
	instance.useAutoRandomSeed = false;
	instance.oneShot = oneShot;
	instance.emitter.gpu = gpu;
	if (definition) {
		instance.emitter.particles.Configure(definition->stateCount);
		instance.emitter.emitTimer = (std::max)(0.0f, definition->settings->emitter.emitInterval);
		instance.emitter.renderSettings = definition->renderSettings;
	}
	if (!gpu) return;
	++gpu->epoch;
	gpu->pending.clear();
	gpu->queuedParents.clear();
	gpu->statistics = {};
	gpu->statisticsValid = false;
	gpu->lastSubmittedSequence = gpu->completedSequence = 0;
	gpu->emissionEndSequence = UINT64_MAX;
}
