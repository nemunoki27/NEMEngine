#include "ParticleGPUConstants.h"

using namespace Engine;

namespace {

	template<typename T>
	T Minimum(const ParticleValue<T>& value) {

		return value.type == ParticleValueType::Constant ? value.constant : value.min;
	}

	template<typename T>
	T Maximum(const ParticleValue<T>& value) {

		return value.type == ParticleValueType::Constant ? value.constant : value.max;
	}

	Vector4 Pack(const Vector3& value) {

		return Vector4(value.x, value.y, value.z, 0);
	}
}

ParticleGPUSimulationConstants Engine::MakeParticleGPUSimulationConstants(const ParticleGPUSimulationStep& step) {

	const auto& settings = *step.definition->settings;
	const auto& emitter = settings.emitter;
	ParticleGPUSimulationConstants constants;
	constants.emitterWorld = step.emitterWorld;
	constants.offsetMinimum = Pack(Minimum(emitter.emitOffset));
	constants.offsetMaximum = Pack(Maximum(emitter.emitOffset));
	constants.speedTime = Vector4(Minimum(emitter.speed), Maximum(emitter.speed), step.deltaTime,
		settings.space == PrimitiveRenderSpace::Screen2D ? 1.0f : 0.0f);
	constants.shapeParameters = Vector4(emitter.sphere.radius, emitter.cone.angle, emitter.cone.radius, emitter.torus.radius);
	constants.shapeSize = Vector4(emitter.box.size.x, emitter.box.size.y, emitter.box.size.z, emitter.torus.thickness);
	constants.pointDirection = Pack(emitter.point.direction);
	constants.circle = Vector4(emitter.circle.angleMin, emitter.circle.angleMax, emitter.circle.stepAngle,
		emitter.circle.clockwise ? -1.0f : 1.0f);
	constants.capacity = emitter.maxParticles;
	constants.phaseCount = static_cast<uint32_t>(step.definition->phases.size());
	constants.spawnCount = step.spawnCount;
	constants.seed = step.seed;
	constants.birthLow = static_cast<uint32_t>(step.birthSequence);
	constants.birthHigh = static_cast<uint32_t>(step.birthSequence >> 32);
	constants.shape = static_cast<uint32_t>(emitter.shape);
	constants.spawnMode = static_cast<uint32_t>(emitter.circle.spawnMode);
	constants.velocityMode = static_cast<uint32_t>(emitter.circle.velocityMode);
	constants.preserveWrapDirection = emitter.circle.usePrevSegmentDirectionOnWrap ? 1u : 0u;
	constants.moduleStateCount = step.program ? step.program->stateCount : 0;
	const bool faces[] = {emitter.box.facePosX, emitter.box.faceNegX, emitter.box.facePosY,
		emitter.box.faceNegY, emitter.box.facePosZ, emitter.box.faceNegZ};
	for (uint32_t i = 0; i < 6; ++i) if (faces[i]) constants.faceMask |= 1u << i;
	if (emitter.shape == ParticleEmitterShape::Circle) constants.shapeParameters.x = emitter.circle.radius;
	if (emitter.shape == ParticleEmitterShape::Rect) {
		constants.shapeSize = Vector4(emitter.rect.size.x, emitter.rect.size.y, 0, 0);
		constants.faceMask = (emitter.rect.edgePosX ? 1u : 0u) | (emitter.rect.edgeNegX ? 2u : 0u) |
			(emitter.rect.edgePosY ? 4u : 0u) | (emitter.rect.edgeNegY ? 8u : 0u);
	}
	return constants;
}
