#include "ParticleGPUWorldCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleWorldModules.h>

// c++
#include <algorithm>
#include <bit>

using namespace Engine;

namespace {

	// 文書と文書内IDの両方でCamera指定を比較する
	bool SameReference(const SceneEntityReference& a, const SceneEntityReference& b) {

		return a.sourceAsset == b.sourceAsset && a.localFileID == b.localFileID && a.prefab == b.prefab;
	}

	// 複数Phaseの衝突入力だけを共有Snapshotへ集約する
	void CollectCollision(const ParticleCollisionSettings& settings, ParticleGPUProgram& program) {

		if (!settings.enabled) return;
		auto& combined = program.collision;
		if (!combined.enabled) combined.layerMask = 0;
		combined.enabled = true;
		combined.sceneColliders |= settings.sceneColliders;
		combined.heightmap |= settings.heightmap;
		combined.includeTriggers |= settings.includeTriggers;
		combined.layerMask |= settings.layerMask;
		combined.radius = (std::max)(combined.radius, settings.radius);
		if (settings.depth) {
			if (program.hasCollisionDepthCamera && !SameReference(program.depthCamera, settings.depthCamera))
				program.unsupportedReason = "同じParticleSystemの深度衝突には共通のCameraを指定してください";
			else {
				program.depthCamera = settings.depthCamera;
				program.hasCollisionDepthCamera = true;
			}
			combined.depth = true;
			combined.depthCamera = program.depthCamera;
		}
	}
}

bool Engine::CompileParticleGPUWorldModule(IParticleModule& module, ParticleGPUInstruction& instruction, ParticleGPUProgram& program) {

	if (const auto* force = dynamic_cast<ParticleForceFieldModule*>(&module)) {
		const auto& settings = force->GetSettings();
		instruction.operation = static_cast<ParticleGPUOperation>(16);
		instruction.flags = settings.useSceneFields ? 1u : 0u;
		instruction.parameter0 = Vector4(settings.strength, settings.range, settings.falloff, static_cast<float>(settings.type));
		instruction.parameter1 = Vector4(settings.center.x, settings.center.y, settings.center.z, 0);
		instruction.parameter2 = Vector4(settings.axis.x, settings.axis.y, settings.axis.z, 0);
		instruction.parameter3.w = std::bit_cast<float>(settings.layerMask);
	} else if (const auto* vector = dynamic_cast<ParticleVectorFieldModule*>(&module)) {
		const auto& settings = vector->GetSettings();
		instruction.operation = static_cast<ParticleGPUOperation>(17);
		instruction.parameter0 = Vector4(settings.intensity, settings.tightness, 0, 0);
		instruction.parameter3.w = std::bit_cast<float>(settings.layerMask);
	} else if (const auto* spline = dynamic_cast<ParticleSplineMovementModule*>(&module)) {
		const auto& settings = spline->GetSettings();
		instruction.operation = static_cast<ParticleGPUOperation>(18);
		instruction.flags = (settings.loop ? 1u : 0u) | (settings.orientToTangent ? 2u : 0u);
		instruction.parameter0 = Vector4(settings.speed, settings.startDistance, settings.orbitRadius, settings.orbitTurnsPerMeter);
		instruction.parameter3.x = static_cast<float>(program.splines.size());
		instruction.parameter3.y = static_cast<float>(program.stateCount++);
		program.splines.push_back(settings.spline);
	} else if (const auto* collision = dynamic_cast<ParticleCollisionModule*>(&module)) {
		const auto& settings = collision->GetSettings();
		instruction.operation = static_cast<ParticleGPUOperation>(22);
		instruction.flags = (settings.enabled ? 1u : 0u) | (settings.sceneColliders ? 2u : 0u) |
			(settings.plane ? 4u : 0u) | (settings.depth ? 8u : 0u) | (settings.heightmap ? 16u : 0u) | (settings.includeTriggers ? 32u : 0u);
		instruction.parameter0 = Vector4(settings.radius, settings.bounce, settings.friction, static_cast<float>(settings.response));
		instruction.parameter1 = Vector4(std::bit_cast<float>(settings.layerMask), settings.depthThickness, 0, 0);
		instruction.parameter2 = Vector4(settings.planeNormal.x, settings.planeNormal.y, settings.planeNormal.z, settings.planeDistance);
		CollectCollision(settings, program);
	} else return false;
	program.usesWorldInputs = true;
	return true;
}
