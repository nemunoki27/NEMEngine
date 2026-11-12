#include "ParticleSimulationReplay.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

void ParticleSimulationReplay::Reset(uint32_t seed, bool oneShot) {

	checkpoints_.clear();
	boundary_.reset();
	settings_.reset();
	seed_ = seed;
	oneShot_ = oneShot;
	initialized_ = partial_ = false;
	feedbackInitialized_ = false;
	feedbackTick_ = 0;
	tick_ = 0;
	time_ = -1;
}

void ParticleSimulationReplay::Evaluate(ECSWorld& world, ParticleInstanceState& instance,
	std::shared_ptr<const ParticleSystemSettings> settings, ParticleSimulationTarget target,
	const ParticlePhaseParentSettings& parent, bool phaseParent, double time,
	uint64_t revision, bool suppressFeedback, const TransformSampler& sampleTransform) {

	if (!std::isfinite(time) || time < 0 || time / kFixedDelta >= static_cast<double>(UINT64_MAX))
		throw std::invalid_argument("粒子の再生時刻が不正です");
	const uint8_t parentSignature = (parent.useEmitter ? 1u : 0u) | (parent.ignoreParentRotation ? 2u : 0u) |
		(parent.ignoreParentScale ? 4u : 0u) | (parent.keepWorldOnDetach ? 8u : 0u) | (phaseParent ? 16u : 0u);
	if (settings_ != settings || instance.target != target || revision_ != revision || parentSignature_ != parentSignature) {
		Reset(seed_, oneShot_);
		settings_ = settings;
		revision_ = revision;
		parentSignature_ = parentSignature;
		ResetParticleSimulation(instance, seed_, oneShot_);
	}
	if (instance.emitter.gpu) instance.emitter.gpu->suppressFeedback = suppressFeedback;
	if (time_ == time && initialized_) return;
	const uint64_t desiredTick = static_cast<uint64_t>(std::floor(time / kFixedDelta + 0.000001));
	const double remainder = (std::max)(0.0, time - desiredTick * kFixedDelta);
	if (initialized_ && (partial_ || desiredTick < tick_)) Restore(instance, desiredTick);
	if (instance.emitter.gpu && !instance.emitter.gpu->pending.empty() &&
		instance.emitter.gpu->pending.back().restoreCheckpoint &&
		!sampleTransform(tick_ * kFixedDelta, instance.emitter.gpu->pending.back().emitterWorld))
		throw std::invalid_argument("粒子の発生元の姿勢を取得できません");
	const auto update = [&](double sampleTime, float delta, uint64_t sequence, bool feedback) {
		Matrix4x4 transform;
		if (!sampleTransform(sampleTime, transform)) throw std::invalid_argument("粒子の発生元の姿勢を取得できません");
		updater_.UpdateInstance(world, instance, settings, transform, parent, phaseParent,
			delta, true, !instance.emissionStopped, false, target);
		if (!instance.rejectedReason.empty()) throw std::invalid_argument(instance.rejectedReason);
		if (instance.emitter.gpu) {
			auto& gpu = *instance.emitter.gpu;
			if (gpu.rejected) throw std::invalid_argument(gpu.diagnostic);
			gpu.deterministicReplay = true;
			gpu.suppressFeedback = suppressFeedback;
			if (!gpu.pending.empty()) {
				gpu.pending.back().randomSequence = sequence;
				gpu.pending.back().suppressFeedback = suppressFeedback || !feedback;
			}
		}
	};
	if (!initialized_) {
		update(0, 0, 0, !feedbackInitialized_);
		feedbackInitialized_ = true;
		initialized_ = true;
		SaveCheckpoint(instance, false);
	}
	while (tick_ < desiredTick) {
		++tick_;
		update(tick_ * kFixedDelta, static_cast<float>(kFixedDelta), tick_, tick_ > feedbackTick_);
		if (tick_ % 15 == 0) SaveCheckpoint(instance, false);
	}
	partial_ = remainder > 0.000001;
	if (partial_) {
		SaveCheckpoint(instance, true);
		update(time, static_cast<float>(remainder), tick_ + 1, false);
	} else boundary_.reset();
	time_ = time;
	feedbackTick_ = (std::max)(feedbackTick_, tick_);
	TrimCheckpoints();
}

uint64_t ParticleSimulationReplay::GetCheckpointMemoryBytes() const {

	uint64_t bytes = boundary_ ? boundary_->state->GetMemoryBytes() : 0;
	for (const auto& checkpoint : checkpoints_) bytes += checkpoint.state->GetMemoryBytes();
	return bytes;
}

void ParticleSimulationReplay::SaveCheckpoint(ParticleInstanceState& instance, bool boundary) {

	if (!boundary) {
		const auto existing = std::find_if(checkpoints_.begin(), checkpoints_.end(),
			[&](const auto& checkpoint) { return checkpoint.tick == tick_; });
		if (existing != checkpoints_.end() && existing->state->CanRestore()) return;
		if (existing != checkpoints_.end()) checkpoints_.erase(existing);
	}
	Checkpoint checkpoint{tick_, std::make_unique<ParticleSimulationCheckpoint>(instance)};
	if (boundary) boundary_ = std::make_unique<Checkpoint>(std::move(checkpoint));
	else checkpoints_.push_back(std::move(checkpoint));
	TrimCheckpoints();
}

void ParticleSimulationReplay::Restore(ParticleInstanceState& instance, uint64_t tick) {

	const Checkpoint* nearest = nullptr;
	const auto consider = [&](const Checkpoint& checkpoint) {
		if (checkpoint.tick <= tick && checkpoint.state->CanRestore() && (!nearest || checkpoint.tick > nearest->tick))
			nearest = &checkpoint;
	};
	for (const auto& checkpoint : checkpoints_) consider(checkpoint);
	if (boundary_) consider(*boundary_);
	if (nearest && nearest->state->Restore(instance)) {
		tick_ = nearest->tick;
		initialized_ = true;
	} else {
		ResetParticleSimulation(instance, seed_, oneShot_);
		tick_ = 0;
		initialized_ = false;
	}
	partial_ = false;
	time_ = -1;
}

void ParticleSimulationReplay::TrimCheckpoints() {

	while (!checkpoints_.empty() && (GetCheckpointCount() > kMaxCheckpoints || GetCheckpointMemoryBytes() > kMemoryBudget))
		checkpoints_.erase(checkpoints_.begin());
	if (boundary_ && GetCheckpointMemoryBytes() > kMemoryBudget) boundary_.reset();
}
