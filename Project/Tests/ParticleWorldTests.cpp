#include "ParticleWorldTests.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUWorldCompiler.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUWorldInputs.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleWorldModules.h>
#include <Engine/Core/Rendering/Particle/ParticleSimulationValidation.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>

// c++
#include <cmath>
#include <limits>
#include <sstream>

using namespace Engine;

namespace {

	// 非一様な制御点間隔を距離で等速に評価する
	bool CheckSpline() {

		SplineData data;
		data.points = {{{0, 0, 0}, {}, {0.1f, 0, 0}}, {{0.3f, 0, 0}, {-0.1f, 0, 0}, {0.9f, 0, 0}},
			{{3, 0, 0}, {-0.9f, 0, 0}, {}}};
		const auto samples = BuildSplineSamples(data, Matrix4x4::MakeScaleMatrix(Vector3(2, 1, 1)));
		if (samples.size() < 3 || std::abs(samples.back().distance - 6) > 0.0001f) return false;
		for (uint32_t i = 0; i <= 12; ++i) {
			const auto sample = EvaluateSplineDistance(samples, i * 0.5f, false);
			if (std::abs(sample.position.x - i * 0.5f) > 0.0001f || sample.tangent.x < 0.99f) return false;
		}
		if (std::abs(EvaluateSplineDistance(samples, -0.5f, true).position.x - 5.5f) > 0.0001f) return false;
		data.closed = true;
		nlohmann::json saved;
		to_json(saved, data);
		SplineData restored;
		from_json(saved, restored);
		return restored.closed && restored.points.size() == data.points.size();
	}

	// 格子順序と境界補間を確認し、不正FGAで以前のデータを失わない
	bool CheckFGA() {

		std::istringstream text("2,1,1, -1,-1,-1, 1,1,1, 0,2,0, 4,2,0,");
		VectorFieldData data;
		std::string diagnostic;
		if (!ReadFGA(text, data, diagnostic)) return false;
		if (!Vector3::NearlyEqual(SampleVectorField(data, Vector3{}), Vector3(2, 2, 0))) return false;
		for (const auto& outside : {Vector3(2, 0, 0), Vector3(-2, 0, 0), Vector3(0, 2, 0), Vector3(0, -2, 0),
			Vector3(0, 0, 2), Vector3(0, 0, -2), Vector3(std::numeric_limits<float>::quiet_NaN(), 0, 0)})
			if (SampleVectorField(data, outside) != Vector3{}) return false;
		std::istringstream truncated("2,1,1,-1,-1,-1,1,1,1,1,2,3");
		if (ReadFGA(truncated, data, diagnostic) || diagnostic.empty() || data.vectors.size() != 2) return false;
		std::istringstream fraction("1.5,1,1,-1,-1,-1,1,1,1,0,0,0");
		return !ReadFGA(fraction, data, diagnostic) && data.resolution.x == 2;
	}

	// Phaseごとの衝突応答を維持し、Cameraの競合を拒否する
	bool CheckCompiler() {

		ParticleGPUProgram program;
		ParticleGPUInstruction instruction;
		ParticleCollisionModule collision;
		ParticleCollisionSettings settings;
		settings.enabled = settings.depth = true;
		settings.radius = 0.3f;
		settings.depthCamera.localFileID = Engine::UUID{5};
		collision.SetSettings(settings);
		if (!CompileParticleGPUWorldModule(collision, instruction, program)) return false;
		if (instruction.parameter0.x != 0.3f || !program.collision.depth || !program.usesWorldInputs) return false;
		settings.radius = 0.8f;
		settings.response = ParticleCollisionResponse::Kill;
		settings.depthCamera.localFileID = Engine::UUID{7};
		collision.SetSettings(settings);
		CompileParticleGPUWorldModule(collision, instruction, program);
		if (program.unsupportedReason.empty() || instruction.parameter0.w != 2 || program.collision.radius != 0.8f) return false;
		ParticleGPUWorldSnapshot snapshot;
		for (uint32_t i = 0; i < 65; ++i) {
			ParticleGPUCollider collider;
			collider.metadata[0] = 1;
			collider.centerRadius = Vector4(static_cast<float>(i) * 4, 0, 0, 1);
			snapshot.colliders.push_back(collider);
		}
		BuildParticleGPUCollisionBVH(snapshot, 0.016f);
		if (snapshot.collisionNodes.size() != 129 || snapshot.collisionNodes.front().escape != 129) return false;
		uint32_t leaves = 0;
		for (size_t i = 0; i < snapshot.collisionNodes.size(); ++i) {
			const auto& node = snapshot.collisionNodes[i];
			if (node.escape <= i || node.escape > snapshot.collisionNodes.size()) return false;
			if (node.collider != UINT32_MAX) ++leaves;
		}
		return leaves == 65;
	}

	// Cameraと曲線を所有Sceneへ限定し、移動形状の速度と世代を保持する
	bool CheckSnapshots() {

		ECSWorld world(ECSWorldKind::Runtime);
		const AssetID asset = ParseAssetID({{"asset", "00000000000000000000000000000078"}}, "asset");
		const auto create = [&](uint64_t instance, uint64_t local) {
			const auto entity = world.CreateEntity();
			world.AddComponent<SceneObjectComponent>(entity);
			world.AddComponent<TransformComponent>(entity);
			auto& scene = world.GetComponent<SceneObjectComponent>(entity);
			scene.sourceAsset = asset;
			scene.sceneInstanceID = Engine::UUID{instance};
			scene.localFileID = Engine::UUID{local};
			return entity;
		};
		const auto ownerA = create(1, 1);
		const auto ownerB = create(2, 1);
		const auto cameraA = create(1, 2);
		const auto cameraB = create(2, 2);
		world.AddComponent<PerspectiveCameraComponent>(cameraA);
		world.AddComponent<PerspectiveCameraComponent>(cameraB);
		const auto curve = create(1, 3);
		world.AddComponent<SplineComponent>(curve);
		world.AddComponent<CollisionComponent>(ownerA);
		ParticleGPUWorldInputCache cache;
		cache.BeginFrame(world, 0, {});
		world.GetComponent<TransformComponent>(ownerA).worldMatrix = Matrix4x4::MakeTranslateMatrix(Vector3(2, 0, 0));
		cache.BeginFrame(world, 1, {});
		ParticleCollisionSettings collision;
		collision.enabled = collision.depth = true;
		collision.depthCamera = SceneEntityReference::FromEntity(world, cameraA);
		const std::vector<SceneEntityReference> curves{SceneEntityReference::FromEntity(world, curve)};
		const auto a = cache.Build(world, ownerA, curves, collision, cameraB);
		const auto b = cache.Build(world, ownerB, curves, collision, cameraA);
		if (a->depth.camera != cameraA || b->depth.camera != cameraB || a->splineRanges[0].count < 2 || b->splineRanges[0].count != 0) return false;
		if (a->colliders.size() != 1 || std::abs(a->colliders[0].velocity.x - 2) > 0.0001f ||
			a->colliders[0].metadata[2] != ownerA.index || a->colliders[0].metadata[3] != ownerA.generation) return false;
		world.DestroyEntity(cameraA);
		const auto missing = cache.Build(world, ownerA, curves, collision, cameraB);
		if (missing->depth.requested || missing->depth.camera.IsValid() || !collision.depthCamera.localFileID) return false;
		auto settings = MakeDefaultParticleSystemSettings();
		settings.phases[0].modules.push_back({"ForceField", {}});
		return !ValidateParticleSimulationSettings(settings, ParticleSimulationTarget::CPU).empty() &&
			ValidateParticleSimulationSettings(settings, ParticleSimulationTarget::GPU).empty();
	}
}

bool NEMTests::TestParticleWorldData() {

	return RunTest("ParticleFGA", CheckFGA) && RunTest("ParticleSpline", CheckSpline) &&
		RunTest("ParticleWorldCompiler", CheckCompiler) && RunTest("ParticleWorldSnapshots", CheckSnapshots);
}
