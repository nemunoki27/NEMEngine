#include "ParticleGPURequests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleParenting.h"
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmissionClock.h>

// c++
#include <algorithm>

using namespace Engine;

namespace {

	Vector4 Pack(const Quaternion& value) {

		return Vector4(value.x, value.y, value.z, value.w);
	}

	uint32_t CalculateSpawnCount(ParticleInstanceState& instance, float deltaTime, bool emissionEnabled) {

		const auto& settings = *instance.definition->settings;
		const auto& emitter = settings.emitter;
		const bool once = instance.oneShot || !settings.looping;
		if (!emissionEnabled || (once && instance.emitter.emitted)) return 0;
		uint32_t emissions = once ? 1u : AdvanceParticleEmissionClock(instance.emitter.emitTimer, emitter.emitInterval, deltaTime);
		instance.emitter.emitted |= emissions != 0;
		uint32_t spawnCount = 0;
		if (emitter.emitCount.type == ParticleValueType::Constant) {
			spawnCount = static_cast<uint32_t>((std::min)(uint64_t(emitter.maxParticles), uint64_t(emissions) * emitter.emitCount.constant));
		} else {
			emissions = (std::min)(emissions, emitter.maxParticles);
			for (uint32_t i = 0; i < emissions && spawnCount < emitter.maxParticles; ++i)
				spawnCount += (std::min)(emitter.emitCount.Sample(), emitter.maxParticles - spawnCount);
		}
		const auto* shape = ParticleEmitterShapeRegistry::GetInstance().Find(emitter.shape);
		return shape && !shape->CanEmit(emitter) ? 0 : spawnCount;
	}

	void FillParents(ParticleGPUSimulationStep& step, const ParticlePhaseParentSettings& overrideSettings,
		bool usePhaseSettings, std::vector<ParticleParentGPU>& previous) {

		std::vector<ParticleParentPose> poses(step.definition->phases.size());
		ParticleParenting::ResolveParticleParents(step.emitterWorld, *step.definition, overrideSettings, usePhaseSettings, poses);
		step.parents.resize(poses.size());
		for (size_t i = 0; i < poses.size(); ++i) {
			auto& parent = step.parents[i];
			const auto& pose = poses[i];
			parent.matrix = pose.matrix;
			parent.inverse = pose.resolved ? Matrix4x4::Inverse(pose.matrix) : Matrix4x4::Identity();
			parent.rotation = Pack(pose.rotation);
			parent.scale = Vector4(pose.scale.x, pose.scale.y, pose.scale.z, 0);
			parent.valid = pose.resolved ? 1u : 0u;
			parent.keepWorldOnDetach = ParticleParenting::ResolveParticleParentSettings(
				step.definition->phases[i], overrideSettings, usePhaseSettings).keepWorldOnDetach ? 1u : 0u;
			const auto& old = i < previous.size() ? previous[i] : parent;
			parent.previousMatrix = old.matrix;
			parent.previousRotation = old.rotation;
			parent.previousScale = old.scale;
			parent.previousValid = old.valid;
		}
		previous = step.parents;
	}
}

bool Engine::QueueParticleGPUUpdate(ParticleInstanceState& instance, const Matrix4x4& emitterWorld,
	const ParticlePhaseParentSettings& parentSettings, bool usePhaseSettings,
	float deltaTime, bool updateSimulation, bool emissionEnabled) {

	if (!instance.emitter.gpu) instance.emitter.gpu = std::make_shared<ParticleGPUState>();
	auto& state = *instance.emitter.gpu;
	if (state.definition != instance.definition) {
		state.definition = instance.definition;
		state.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*instance.definition));
		state.rejected = false;
		state.diagnostic.clear();
		if (!state.program->unsupportedReason.empty()) state.Reject(state.program->unsupportedReason);
	}
	if (state.rejected) return false;
	// 放出終了以後の結果が届くまで、空になったと判断しない
	if (state.emissionEndSequence != UINT64_MAX && state.statisticsValid &&
		state.completedSequence >= state.emissionEndSequence &&
		state.statistics.particleCount == 0 && state.statistics.trailCount == 0) return true;

	ParticleGPUSimulationStep step;
	step.definition = instance.definition;
	step.program = state.program;
	step.emitterWorld = emitterWorld;
	step.seed = instance.randomSeed;
	step.deltaTime = updateSimulation ? deltaTime : 0.0f;
	step.spawnCount = updateSimulation ? CalculateSpawnCount(instance, deltaTime, emissionEnabled) : 0;
	step.birthSequence = instance.emitter.nextParticleID;
	step.sequence = ++state.nextSequence;
	instance.emitter.nextParticleID += step.spawnCount;
	FillParents(step, parentSettings, usePhaseSettings, state.queuedParents);
	const auto& settings = *instance.definition->settings;
	if (state.emissionEndSequence == UINT64_MAX &&
		(instance.emissionStopped || ((instance.oneShot || !settings.looping) && instance.emitter.emitted)))
		state.emissionEndSequence = step.sequence;
	state.pending.push_back(std::move(step));
	return false;
}
