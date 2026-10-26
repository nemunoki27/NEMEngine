#include "ParticleGPUMovementCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUCurveCompiler.h"
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCurlNoiseModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSpiralMovementModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticlePendulumMovementModule.h>

using namespace Engine;

void Engine::CompileParticleGPUMovement(IParticleModule& module, ParticleGPUInstruction& instruction, ParticleGPUProgram& program) {

	if (instruction.operation == ParticleGPUOperation::CurlNoise) {
		program.usesCurlNoise = true;
		const auto& settings = static_cast<ParticleCurlNoiseModule&>(module).GetSettings();
		instruction.parameter0 = Vector4(settings.strength, settings.frequency, 0, 0);
		instruction.parameter1 = Vector4(settings.scrollSpeed.x, settings.scrollSpeed.y, settings.scrollSpeed.z, 0);
		return;
	}

	// ノイズは位置と時間に依存するためGPUで直接評価する
	if (instruction.operation == ParticleGPUOperation::NoiseForce) {
		const auto& settings = static_cast<ParticleNoiseForceModule&>(module).GetSettings();
		instruction.parameter0 = Vector4(settings.strength, settings.frequency, 0, 0);
		return;
	}
	if (instruction.operation == ParticleGPUOperation::NoiseUV) {
		const auto& settings = static_cast<ParticleNoiseUVModule&>(module).GetSettings();
		instruction.parameter0 = Vector4(settings.strength, settings.frequency, 0, 0);
		return;
	}

	// 発生方向は運動を使うSystemだけで保持する
	if (program.spawnDirectionSlot == UINT32_MAX) program.spawnDirectionSlot = program.stateCount++;
	if (instruction.operation == ParticleGPUOperation::Spiral) {
		const auto& settings = static_cast<ParticleSpiralMovementModule&>(module).GetSettings();
		instruction.flags = settings.reverse ? 1u : 0u;
		instruction.parameter0 = Vector4(settings.startAngle, settings.particleAngleOffset, 0, 0);
		CompileParticleGPUCurve(module.ToJson(), [&](float progress) {
			ParticleGPUCurveSample sample;
			sample.value0.x = ParticleFloatAnimation::EvaluateAnimation(settings.radius, progress);
			sample.value0.y = ParticleFloatAnimation::EvaluateAnimation(settings.turns, progress);
			return sample;
		}, instruction, program);
		return;
	}
	const auto& settings = static_cast<ParticlePendulumMovementModule&>(module).GetSettings();
	instruction.flags = settings.reverse ? 1u : 0u;
	instruction.parameter0 = Vector4(settings.startPhase, settings.particlePhaseOffset, settings.planeAngle, settings.arcStrength);
	CompileParticleGPUCurve(module.ToJson(), [&](float progress) {
		ParticleGPUCurveSample sample;
		sample.value0.x = ParticleFloatAnimation::EvaluateAnimation(settings.length, progress);
		sample.value0.y = ParticleFloatAnimation::EvaluateAnimation(settings.maxAngle, progress);
		sample.value0.z = ParticleFloatAnimation::EvaluateAnimation(settings.cycles, progress);
		return sample;
	}, instruction, program);
}
