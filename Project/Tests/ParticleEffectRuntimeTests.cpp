#include "ParticleEffectRuntimeTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleLookToVelocityModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmissionClock.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleTrailDataBuilder.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Systems/Effect/ParticleInstanceUpdater.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>
#include <Engine/Editor/Tools/Builtin/Effect/ParticleEffectEditSession.h>

// c++
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
		Particle particle{};
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

	ParticleEffectAsset MakeEffect() {

		ParticleEffectAsset effect{};
		effect.name = "Runtime contract";
		ParticleEffectGroup group{};
		group.shape = PrimitiveType::Ring;
		group.ring.outerRadius = 2.0f;
		group.ring.innerRadius = 1.0f;
		group.emitter.emitCount = ParticleValue<uint32_t>{ 4u };
		for (int i = 0; i < 3; ++i) {
			ParticleEffectPhase phase{};
			phase.lifetime = ParticleValue<float>{ 0.2f };
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
		group.phases[1].modules.push_back({ "ShapeOverLifetime", shape.ToJson() });
		ParticleRotationModule rotation;
		auto rotationSettings = rotation.GetSettings();
		rotationSettings.fixedAngle = ParticleValue<Vector3>{ Vector3(0.0f, 0.0f, 90.0f) };
		rotation.SetSettings(rotationSettings);
		group.phases[2].modules.push_back({ "Rotation", rotation.ToJson() });
		effect.groups.push_back(std::move(group));
		return effect;
	}

	bool TestPublishedDefinition(AssetDatabase& database, AssetID effectID, const ParticleEffectAsset& saved) {

		ParticleEffectDefinitionCache definitions;
		SystemContext context{};
		context.assetDatabase = &database;
		const auto* definition = definitions.ResolveEffect(context, effectID, false);
		if (!definition || definition->asset.name != saved.name) return false;
		const uint64_t originalRevision = definition->revision;
		ParticleEffectAsset draft = saved;
		draft.name = "Draft";
		auto& bridge = ParticleEffectEditBridge::GetInstance();
		bridge.Push(effectID, draft);
		definition = definitions.ResolveEffect(context, effectID, false);
		if (!definition || definition->asset.name != draft.name || definition->revision <= originalRevision) return false;
		ParticleEffectDefinitionCache anotherConsumer;
		const auto* another = anotherConsumer.ResolveEffect(context, effectID, false);
		if (!another || another->asset.name != draft.name) return false;
		const uint64_t draftRevision = bridge.GetRevision(effectID);
		bridge.Remove(effectID);
		if (bridge.GetRevision(effectID) <= draftRevision) return false;
		definition = definitions.ResolveEffect(context, effectID, false);
		if (!definition || definition->asset.name != saved.name) return false;
		// 解除後の再編集でも既読番号と衝突しない
		bridge.Push(effectID, draft);
		definition = definitions.ResolveEffect(context, effectID, false);
		if (!definition || definition->asset.name != draft.name) return false;
		bridge.Remove(effectID);
		definition = definitions.ResolveEffect(context, effectID, false);
		const uint64_t validRevision = definition->revision;
		const auto path = database.ResolveFullPath(effectID);
		if (!JsonAdapter::SaveCanonical(path, nlohmann::json::object())) return false;
		database.NotifyContentChanged(effectID);
		definition = definitions.ResolveEffect(context, effectID, true);
		if (!definition || definition->revision != validRevision || definition->asset.name != saved.name) return false;
		if (!JsonAdapter::SaveCanonical(path, ToJson(saved))) return false;
		database.NotifyContentChanged(effectID);
		definition = definitions.ResolveEffect(context, effectID, false);
		return definition && definition->revision > validRevision;
	}

	bool TestInstanceShapeAndSeed(AssetDatabase& database, AssetID effectID) {

		ECSWorld world(ECSWorldKind::Runtime);
		SystemContext context{};
		context.assetDatabase = &database;
		context.mode = WorldMode::Play;
		ParticleInstanceUpdater updater;
		ParticleEffectInstanceRuntime first{};
		first.effect = effectID;
		first.oneShot = true;
		first.useAutoRandomSeed = false;
		first.randomSeed = 417u;
		ParticleEffectInstanceRuntime second = first;
		ParticleEffectInstanceRuntime unrelated = first;
		unrelated.randomSeed = 29u;
		const auto update = [&](ParticleEffectInstanceRuntime& instance, float deltaTime) {

			return updater.UpdateEffectInstance(world, instance, Matrix4x4::Identity(), {}, true,
				context, deltaTime, true, true, false, false);
		};
		if (update(first, 0.01f) || update(unrelated, 0.01f) || update(second, 0.01f)) return false;
		if (first.runtimeGroups.size() != 1 || first.runtimeGroups[0].particles.size() != 4 ||
			second.runtimeGroups[0].particles.size() != 4) return false;
		for (size_t i = 0; i < 4; ++i) {
			const auto& a = first.runtimeGroups[0].particles[i];
			const auto& b = second.runtimeGroups[0].particles[i];
			if (a.pos != b.pos || a.velocity != b.velocity || a.shapeData.params0.x != 2.0f || !a.hasParent) return false;
		}
		if (update(first, 0.21f)) return false;
		for (const auto& particle : first.runtimeGroups[0].particles) {
			if (particle.phaseIndex != 1 || particle.shapeData.params0.x != 9.0f) return false;
		}
		if (update(first, 0.21f)) return false;
		for (const auto& particle : first.runtimeGroups[0].particles) {
			if (particle.phaseIndex != 2 || particle.shapeData.params0.x != 9.0f) return false;
			Vector3 rotated = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), Quaternion::MakeRotateMatrix(particle.rotation));
			if (std::abs(rotated.x) > 0.0001f || std::abs(rotated.y - 1.0f) > 0.0001f) return false;
		}
		return true;
	}

	bool TestDraftHistoryAndSave(AssetDatabase& database, AssetID effectID) {

		EditorToolContext context{};
		context.toolContext.assetDatabase = &database;
		ParticleEffectEditSession session;
		std::string status;
		if (!session.LoadEffect(context, effectID, status)) return false;
		const auto saved = ToJson(session.GetDraft());
		const Engine::UUID phaseID = session.GetDraft().groups[0].phases[1].id;
		session.GetDraft().name = "Drag start";
		session.UpdateEditing(true, true);
		session.GetDraft().name = "Drag end";
		session.UpdateEditing(true, false);
		if (!session.IsDirty() || !session.Undo() || session.CanUndo() || session.IsDirty() ||
			ToJson(session.GetDraft()) != saved) return false;
		if (!session.Redo() || session.GetDraft().name != "Drag end" ||
			session.GetDraft().groups[0].phases[1].id != phaseID) return false;
		const auto path = database.ResolveFullPath(effectID);
		{
			// 保存失敗でもdraftとUndoを破棄しない
			NEMTests::TestFileReadLock lock(path);
			if (session.SaveEffect(context, status) || !session.IsDirty() || !session.CanUndo()) return false;
		}
		if (!session.SaveEffect(context, status) || session.IsDirty()) return false;
		session.GetDraft().name = "Unsaved";
		session.UpdateEditing(true, false);
		if (!JsonAdapter::SaveCanonical(path, nlohmann::json::object())) return false;
		if (session.LoadEffect(context, effectID, status) || !session.IsDirty() ||
			session.GetDraft().name != "Unsaved") return false;
		session.Discard();
		if (session.IsDirty() || session.GetDraft().name != "Drag end" || session.CanUndo()) return false;
		return JsonAdapter::SaveCanonical(path, saved);
	}

	bool TestTrailEndpoints() {

		ECSWorld world(ECSWorldKind::Runtime);
		Entity entity = world.CreateEntity();
		world.AddComponent<ParticleSystemComponent>(entity);
		auto* runtime = TryGetParticleSystemRuntime(world, entity);
		if (!runtime) return false;
		ParticleGroupRuntimeState group{};
		const auto effect = MakeEffect();
		group.groupID = effect.groups[0].id;
		group.renderSettings = MakeParticleRenderSettings(effect.space, effect.groups[0]);
		group.renderSettings.trail.enabled = true;
		ParticleTrailRuntime trail{};
		trail.points = { { Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0 },
			{ Vector3(0.0f, 0.0f, 0.0f), 0.0f, 0 }, { Vector3(1.0f, 0.0f, 0.0f), 0.0f, 0 } };
		trail.head = { Vector3(2.0f, 0.0f, 0.0f), 0.0f, 0 };
		group.trails.emplace(1, std::move(trail));
		runtime->effect.runtimeGroups.push_back(std::move(group));
		RenderSceneBatch batch;
		RenderItem item{};
		item.world = &world;
		item.entity = entity;
		item.payload = batch.PushPayload(ParticleRenderPayload{ runtime->effect.runtimeGroups[0].groupID, 0 });
		const RenderItem* items[] = { &item };
		RenderDrawContext context{};
		context.batch = &batch;
		ParticleTrailRenderData data;
		for (float lifetime : { 0.0f, 1.0f }) {
			// 重複点を除去してもTextureの全範囲を使う
			runtime->effect.runtimeGroups[0].renderSettings.trail.pointLifetime = lifetime;
			ParticleTrailDataBuilder::Build(context, items, {}, data);
			if (data.points.size() != 3 || data.segments.size() != 2 || data.points.front().ribbonT != 0.0f ||
				data.points.back().ribbonT != 1.0f || data.points[1].ribbonT != 0.5f) return false;
		}
		// Trailが残る間は停止済みでもAliveとして扱う
		if (!IsParticleSystemAlive(world, entity)) return false;
		runtime->effect.runtimeGroups[0].trails.clear();
		return !IsParticleSystemAlive(world, entity);
	}
}

bool NEMTests::TestParticleEffectRuntime() {

	using namespace Engine;
	TestDirectory directory("ParticleEffect", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Runtime.effect.json";
	const ParticleEffectAsset effect = MakeEffect();
	const nlohmann::json saved = ToJson(effect);
	ParticleEffectAsset restored{};
	if (!FromJson(saved, restored) || ToJson(restored) != saved) return false;
	nlohmann::json duplicate = saved;
	duplicate["groups"][0]["phases"][1]["id"] = duplicate["groups"][0]["phases"][0]["id"];
	if (FromJson(duplicate, restored) || ToJson(restored) != saved) return false;
	// JSONの型エラーでも読込済みの設定を保つ
	nlohmann::json invalid = saved;
	invalid["groups"][0]["enabled"] = "invalid";
	try {
		FromJson(invalid, restored);
		return false;
	} catch (const nlohmann::json::exception&) {
		if (ToJson(restored) != saved) return false;
	}
	if (!JsonAdapter::SaveCanonical(path, saved)) return false;
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({ directory.GetPath() })) return false;
	const auto* meta = database.FindByPath(RuntimePaths::ToAssetPath(path));
	if (!meta) return false;
	const AssetID effectID = meta->guid;
	return TestEmissionAndRotation() && TestCurveAndCustomParameter() && TestTrailEndpoints() &&
		TestPublishedDefinition(database, effectID, effect) && TestInstanceShapeAndSeed(database, effectID) &&
		TestDraftHistoryAndSave(database, effectID);
}
