#include "ParticleSystemRuntimeTests.h"
#include "TestRunner.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleLookToVelocityModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmissionClock.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleTrailDataBuilder.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Systems/Effect/ParticleInstanceUpdater.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticleSettingsEditSession.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

	using namespace Engine;

	bool TestEmissionAndRotation() {

		// 分割frameでも発生回数と端数を保持する
		float timer = 0.0f;
		if (AdvanceParticleEmissionClock(timer, 0.1f, 0.25f) != 2 || std::abs(timer - 0.05f) > 0.00001f ||
			AdvanceParticleEmissionClock(timer, 0.1f, 0.06f) != 1 || std::abs(timer - 0.01f) > 0.00001f) return false;
		if (AdvanceParticleEmissionClock(timer, 0.0f, 1.0f) != 1 || timer != 0.0f) return false;
		ParticleLoopSettings loop{ 3, ParticleLoopType::Repeat };
		if (loop.LoopedT(1.0f) != 1.0f || loop.LoopedT(-1.0f) != 0.0f ||
			loop.LoopedT(std::numeric_limits<float>::quiet_NaN()) != 0.0f) return false;
		loop.type = ParticleLoopType::PingPong;
		if (loop.LoopedT(1.0f) != 1.0f) return false;
		loop.loopCount = 2;
		if (loop.LoopedT(1.0f) != 0.0f) return false;

		// 発生面を全て無効にした形状は発生不可にする
		ParticleEmitterSettings emitter{};
		emitter.box.facePosX = emitter.box.faceNegX = emitter.box.facePosY = emitter.box.faceNegY =
			emitter.box.facePosZ = emitter.box.faceNegZ = false;
		emitter.rect.edgePosX = emitter.rect.edgeNegX = emitter.rect.edgePosY = emitter.rect.edgeNegY = false;
		if (ParticleBoxEmitterShape{}.CanEmit(emitter) || ParticleRectEmitterShape{}.CanEmit(emitter)) return false;
		emitter.box.faceNegZ = true;
		if (!ParticleBoxEmitterShape{}.CanEmit(emitter)) return false;

		// 同じPhaseのRotationがそれぞれの速度を使う
		ParticleRotationModule first;
		ParticleRotationModule second;
		auto settings = first.GetSettings();
		settings.mode = ParticleRotationMode::Additive;
		settings.rotationSpeed = ParticleValue<Vector3>{ Vector3(0.0f, 0.0f, 30.0f) };
		first.SetSettings(settings);
		settings.rotationSpeed = ParticleValue<Vector3>{ Vector3(0.0f, 0.0f, 60.0f) };
		second.SetSettings(settings);
		first.SetStateSlot(0);
		second.SetStateSlot(1);
		std::array<Vector3, 2> states{};
		Particle particle{};
		particle.rotationSpeeds = states;
		first.OnSpawn(particle);
		second.OnSpawn(particle);
		first.OnUpdate(particle, 1.0f);
		second.OnUpdate(particle, 1.0f);
		Vector3 direction = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), Quaternion::MakeRotateMatrix(particle.rotation));
		if (std::abs(direction.x) > 0.0001f || std::abs(direction.y - 1.0f) > 0.0001f) return false;
		Quaternion previous = particle.rotation;
		ParticleLookToVelocityModule{}.OnUpdate(particle, 0.1f);
		return particle.rotation == previous;
	}

	bool TestCurveAndCustomParameter() {

		// 未整列の軸キーでも設定との対応を保つ
		ParticleRotationModule rotation;
		nlohmann::json params = rotation.ToJson();
		params["quaternionCurveChannels"][0]["keys"] = nlohmann::json::array({
			{ { "time", 1.0f }, { "value", 1.0f } }, { { "time", 0.0f }, { "value", 0.0f } } });
		params["quaternionCurveAxisKeys"] = nlohmann::json::array({
			{ { "useCustomAxis", true }, { "customAxis", Vector3(0.0f, 1.0f, 0.0f).ToJson() } },
			{ { "useCustomAxis", true }, { "customAxis", Vector3(1.0f, 0.0f, 0.0f).ToJson() } } });
		rotation.FromJson(params);
		const auto& curve = rotation.GetSettings().quaternionCurve;
		if (curve.channels[0].keys[0].time != 0.0f || curve.axisKeys[0].customAxis != Vector3(1.0f, 0.0f, 0.0f) ||
			curve.axisKeys[1].customAxis != Vector3(0.0f, 1.0f, 0.0f)) return false;

		// 明示的な0は保存して未指定の値と区別する
		ParticleCustomShaderParameterModule custom;
		custom.SetParameters({ { "dissolve", ParticleMaterialAnimatedParameter{} } });
		ParticleCustomShaderParameterModule restored;
		restored.FromJson(custom.ToJson());
		return restored.GetParameters().contains("dissolve") &&
			restored.GetParameters().at("dissolve").constant.x == 0.0f &&
			!restored.GetParameters().contains("edgeWidth");
	}

	bool TestEmissiveCurrentColor() {

		ParticleEmissiveModule emissive;
		if (!emissive.GetSettings().useParticleColor) return false;
		auto emission = emissive.GetSettings();
		emission.startColor = Color3(0.9f, 0.1f, 0.3f);
		emission.endColor = Color3(0.1f, 0.5f, 0.7f);
		emission.startIntensity = 2.0f;
		emission.endIntensity = 6.0f;
		emission.colorEasingType = EasingType::Linear;
		emission.intensityEasingType = EasingType::Linear;
		ParticleColorOverLifetimeModule color;
		auto colorSettings = color.GetSettings();
		colorSettings.startColor = Color4(0.2f, 0.4f, 0.8f, 0.1f);
		colorSettings.endColor = Color4(0.8f, 0.6f, 0.2f, 0.7f);
		colorSettings.easingType = EasingType::Linear;
		color.SetSettings(colorSettings);
		ParticleDefinitionCompiler compiler;
		// 参照の切替と並び替えを、通常更新と一括更新の両方で確認する
		for (bool follow : {false, true}) for (bool reverse : {false, true}) {
			emission.useParticleColor = follow;
			emissive.SetSettings(emission);
			ParticleEmissiveModule restored;
			restored.FromJson(emissive.ToJson());
			if (restored.GetSettings().useParticleColor != follow) return false;
			auto settings = MakeDefaultParticleSystemSettings();
			settings.phases[0].modules = {{"ColorOverLifetime", color.ToJson()}, {"Emissive", emissive.ToJson()}};
			if (reverse) std::reverse(settings.phases[0].modules.begin(), settings.phases[0].modules.end());
			const auto definition = compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings));
			for (float age : {0.0f, 0.25f, 1.0f}) {
				Particle single;
				single.age = age;
				Particle batch = single;
				ParticleModuleExecution::ApplyUpdateModules(single, definition->phases[0], 0.0f);
				ParticleModuleExecution::ExecuteUpdateModules(std::span<Particle>(&batch, 1), definition->phases[0], 0.0f);
				const Color4 expectedColor = Color4::Lerp(colorSettings.startColor, colorSettings.endColor, age);
				const Color3 expectedRGB = follow ? Color3(expectedColor.r, expectedColor.g, expectedColor.b) :
					Color3::Lerp(emission.startColor, emission.endColor, age);
				const Vector4 expected(expectedRGB.r, expectedRGB.g, expectedRGB.b, 2.0f + 4.0f * age);
				const Vector4 difference = single.emissive - expected;
				if ((std::max)({std::abs(difference.x), std::abs(difference.y), std::abs(difference.z), std::abs(difference.w)}) >
					0.0001f || batch.emissive != single.emissive) return false;
			}
		}
		// 未指定の保存値は既定の参照状態へ戻す
		emission.useParticleColor = false;
		emissive.SetSettings(emission);
		emissive.FromJson(nlohmann::json::object());
		if (!emissive.GetSettings().useParticleColor) return false;
		Particle particle;
		particle.color = Color4(0.3f, 0.6f, 0.9f, 0.0f);
		emissive.OnUpdate(particle, 0.0f);
		return particle.emissive == Vector4(0.3f, 0.6f, 0.9f, 2.0f);
	}

	ParticleSystemSettings MakeSettings() {

		ParticleSystemSettings group;
		group.shape = PrimitiveType::Ring;
		group.ring.outerRadius = 2.0f;
		group.ring.innerRadius = 1.0f;
		group.emitter.emitCount = ParticleValue<uint32_t>{4u};
		for (int i = 0; i < 3; ++i) {
			ParticlePhaseSettings phase;
			phase.lifetime = ParticleValue<float>{0.2f};
			phase.lifeEndMode = i < 2 ? ParticleLifeEndMode::Advance : ParticleLifeEndMode::Clamp;
			phase.parentSettings.useEmitter = true;
			group.phases.push_back(std::move(phase));
		}
		ParticleShapeOverLifetimeModule shape;
		shape.FromJson(nlohmann::json::object());
		auto settings = shape.GetSettings();
		auto& radius = settings.parameters.at(ParticleShapeAnimation::kRingOuterRadius);
		radius.mode = ParticleMaterialParameterMode::Constant;
		radius.constant.x = 9.0f;
		shape.SetSettings(settings);
		group.phases[1].modules.push_back({"ShapeOverLifetime", shape.ToJson()});
		ParticleRotationModule rotation;
		auto rotationSettings = rotation.GetSettings();
		rotationSettings.fixedAngle = ParticleValue<Vector3>{Vector3(0.0f, 0.0f, 90.0f)};
		rotation.SetSettings(rotationSettings);
		group.phases[2].modules.push_back({"Rotation", rotation.ToJson()});
		return group;
	}

	bool TestInstanceShapeAndSeed() {

		ECSWorld world(ECSWorldKind::Runtime);
		ParticleInstanceUpdater updater;
		auto settings = std::make_shared<const ParticleSystemSettings>(MakeSettings());
		ParticleInstanceState first;
		first.oneShot = true;
		first.useAutoRandomSeed = false;
		first.randomSeed = 417u;
		ParticleInstanceState second;
		second.oneShot = true;
		second.useAutoRandomSeed = false;
		second.randomSeed = first.randomSeed;
		ParticleInstanceState unrelated;
		unrelated.oneShot = true;
		unrelated.useAutoRandomSeed = false;
		unrelated.randomSeed = 29u;
		const auto update = [&](ParticleInstanceState& instance, float deltaTime) {
			return updater.UpdateInstance(world, instance, settings, Matrix4x4::Identity(), {}, true,
				deltaTime, true, true, false);
		};
		if (update(first, 0.01f) || update(unrelated, 0.01f) || update(second, 0.01f)) return false;
		if (first.emitter.particles.size() != 4 || second.emitter.particles.size() != 4) return false;
		for (size_t i = 0; i < 4; ++i) {
			const auto& a = first.emitter.particles[i];
			const auto& b = second.emitter.particles[i];
			if (a.pos != b.pos || a.velocity != b.velocity || a.shapeData.params0.x != 2.0f || !a.hasParent) return false;
		}
		if (update(first, 0.21f)) return false;
		for (const auto& particle : first.emitter.particles)
			if (particle.phaseIndex != 1 || particle.shapeData.params0.x != 9.0f) return false;
		if (update(first, 0.21f)) return false;
		for (const auto& particle : first.emitter.particles) {
			if (particle.phaseIndex != 2 || particle.shapeData.params0.x != 9.0f) return false;
			const auto rotated = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), Quaternion::MakeRotateMatrix(particle.rotation));
			if (std::abs(rotated.x) > 0.0001f || std::abs(rotated.y - 1.0f) > 0.0001f) return false;
		}
		// 数値変更は粒子を維持し、構成変更は対象Systemだけ再開する
		auto numeric = std::make_shared<ParticleSystemSettings>(*settings);
		numeric->emitter.speed = ParticleValue<float>{12.0f};
		settings = numeric;
		update(first, 0.01f);
		if (first.emitter.particles[0].phaseIndex != 2) return false;
		auto structural = std::make_shared<ParticleSystemSettings>(*settings);
		structural->phases[0].modules.push_back({"ColorOverLifetime", nlohmann::json::object()});
		settings = structural;
		update(first, 0.01f);
		return first.emitter.particles.size() == 4 && first.emitter.particles[0].phaseIndex == 0 &&
			second.emitter.particles[0].phaseIndex == 0;
	}

	bool TestPauseAndParent() {

		ECSWorld world(ECSWorldKind::Runtime);
		const Entity entity = world.CreateEntity();
		world.AddComponent<TransformComponent>(entity);
		world.AddComponent<ParticleSystemComponent>(entity);
		world.GetComponent<ParticleSystemComponent>(entity).simulationSpace = ParticleSystemSimulationSpace::Local;
		auto settings = MakeSettings();
		settings.emitter.speed = ParticleValue<float>{0.0f};
		settings.trail.enabled = true;
		SetParticleSystemSettings(world, entity, settings);
		ParticleSystem system;
		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = context.unscaledDeltaTime = 0.01f;
		system.Update(world, context);
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || runtime->instance.emitter.particles.empty()) return false;
		const auto before = runtime->instance.emitter.particles[0];
		RequestParticleSystemPause(world, entity);
		world.GetComponent<TransformComponent>(entity).localPos.x = 5.0f;
		system.Update(world, context);
		const auto paused = runtime->instance.emitter.particles[0];
		// 時間は止め、親の移動だけを反映する
		if (!runtime->paused || paused.age != before.age || paused.phaseIndex != before.phaseIndex ||
			std::abs(paused.worldPos.x - before.worldPos.x - 5.0f) > 0.0001f) return false;
		const auto* trail = runtime->instance.emitter.trails.TryGet({paused.trailIndex, paused.trailGeneration});
		if (!trail || trail->head.position != paused.worldPos ||
			runtime->instance.emitter.trails.GetPoint(*trail, 0).age != 0.0f) return false;
		RequestParticleSystemPlay(world, entity);
		system.Update(world, context);
		return runtime->playing && !runtime->paused && runtime->instance.emitter.particles[0].age > paused.age;
	}

	bool TestGPURejectionAndRetry() {

		ECSWorld world(ECSWorldKind::Runtime);
		const Entity entity = world.CreateEntity();
		world.AddComponent<ParticleSystemComponent>(entity);
		world.GetComponent<ParticleSystemComponent>(entity).simulationTarget = ParticleSimulationTarget::GPU;
		auto settings = MakeSettings();
		settings.trail.enabled = true;
		settings.trail.maxPoints = 1025;
		SetParticleSystemSettings(world, entity, settings);
		ParticleSystem system;
		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = context.unscaledDeltaTime = 0.01f;
		system.Update(world, context);
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime || !runtime->instance.emitter.gpu || !runtime->instance.emitter.gpu->rejected ||
			!runtime->instance.emitter.particles.empty() || !IsParticleSystemStopped(world, entity)) return false;
		const auto rejected = runtime->instance.emitter.gpu;
		RequestParticleSystemPlay(world, entity);
		system.Update(world, context);
		if (runtime->instance.emitter.gpu == rejected) return false;
		// CPUへの変更は明示操作だけで行う
		world.GetComponent<ParticleSystemComponent>(entity).simulationTarget = ParticleSimulationTarget::CPU;
		system.Update(world, context);
		return !runtime->instance.emitter.gpu && runtime->instance.emitter.particles.size() == 4;
	}

	bool TestSettingsOwnership() {

		ECSWorld world(ECSWorldKind::Authoring);
		const Entity source = world.CreateEntity();
		world.AddComponent<ParticleSystemComponent>(source);
		auto settings = MakeSettings();
		SetParticleSystemSettings(world, source, settings);
		nlohmann::json saved;
		if (!world.SerializeComponentToJson(source, "ParticleSystem", saved)) return false;
		const Entity duplicate = world.CreateEntity();
		if (!world.AddComponentFromJson(duplicate, "ParticleSystem", saved)) return false;
		settings.emitter.maxParticles = 91;
		SetParticleSystemSettings(world, duplicate, settings);
		if (GetParticleSystemSettings(world, source)->emitter.maxParticles == 91) return false;
		// 保存用の複製は元Worldの破棄後も設定を保持する
		auto snapshot = world.CloneForSerialization();
		world.DestroyEntity(source);
		nlohmann::json cloned;
		if (!snapshot->SerializeComponentToJson(source, "ParticleSystem", cloned) || cloned != saved) return false;
		if (!world.ApplyComponentJson(duplicate, "ParticleSystem", saved)) return false;
		nlohmann::json restored;
		if (!world.SerializeComponentToJson(duplicate, "ParticleSystem", restored) || restored != saved) return false;
		std::unique_ptr<ECSWorld> detached;
		Entity original;
		{
			ECSWorld temporary(ECSWorldKind::Authoring);
			original = temporary.CreateEntity();
			temporary.AddComponentFromJson(original, "ParticleSystem", saved);
			detached = temporary.CloneForSerialization();
		}
		nlohmann::json orphaned;
		return detached->SerializeComponentToJson(original, "ParticleSystem", orphaned) && orphaned == saved;
	}
}

bool NEMTests::TestParticleSystemRuntime() {

	using namespace Engine;
	const auto settings = MakeSettings();
	const auto saved = ToJson(settings);
	ParticleSystemSettings restored;
	if (!FromJson(saved, restored) || ToJson(restored) != saved) return false;
	auto duplicate = saved;
	duplicate["phases"][1]["id"] = duplicate["phases"][0]["id"];
	if (FromJson(duplicate, restored) || ToJson(restored) != saved) return false;
	return RunTest("TestEmissionAndRotation", TestEmissionAndRotation) &&
		RunTest("TestCurveAndCustomParameter", TestCurveAndCustomParameter) &&
		RunTest("TestEmissiveCurrentColor", TestEmissiveCurrentColor) &&
		RunTest("TestInstanceShapeAndSeed", TestInstanceShapeAndSeed) &&
		RunTest("TestPauseAndParent", TestPauseAndParent) &&
		RunTest("TestGPURejectionAndRetry", TestGPURejectionAndRetry) &&
		RunTest("TestSettingsOwnership", TestSettingsOwnership) &&
		RunTest("TestParticleSystemTrails", TestParticleSystemTrails) &&
		RunTest("TestParticleGPUProgram", TestParticleGPUProgram);
}
