#include "ParticleSourceTests.h"
#include "TestRunner.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/ParticleMotionSettings.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotBatchDuplicator.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabSnapshotBuilder.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

using namespace Engine;

namespace {

	// 同名の別Instanceを選ばず、明示した階層から対象を取得する
	Entity FindNamed(const ECSWorld& world, std::span<const Entity> entities, const char* name) {

		for (const auto entity : entities) {
			const auto* component = world.TryGetComponent<NameComponent>(entity);
			if (component && component->name == name) return entity;
		}
		return Entity::Null();
	}

	// Sceneで作成した参照を全ての粒子入力へ設定する
	void SetReferences(ECSWorld& world, Entity owner, std::span<const Entity> entities) {

		auto settings = MakeDefaultParticleSystemSettings();
		settings.source.kind = ParticleSourceKind::MeshSurface;
		settings.source.renderer = SceneEntityReference::FromEntity(world, FindNamed(world, entities, "Renderer"));
		settings.source.camera = SceneEntityReference::FromEntity(world, FindNamed(world, entities, "Camera"));
		ParticleSplineMovementSettings spline;
		spline.spline = SceneEntityReference::FromEntity(world, FindNamed(world, entities, "Curve"));
		ParticleCollisionSettings collision;
		collision.enabled = collision.depth = true;
		collision.depthCamera = settings.source.camera;
		settings.phases[0].modules = {{"SplineMovement", spline}, {"Collision", collision}};
		ParticleEventRouteSettings route;
		route.target = SceneEntityReference::FromEntity(world, FindNamed(world, entities, "Child"));
		settings.feedback.events = {route};
		SetParticleSystemSettings(world, owner, settings);
	}

	// 展開または複製した階層の内部だけへ参照が向くことを確認する
	bool CheckReferences(const ECSWorld& world, Entity owner, std::span<const Entity> entities) {

		const auto settings = GetParticleSystemSettings(world, owner);
		if (!settings || settings->phases.size() != 1 || settings->phases[0].modules.size() != 2 || settings->feedback.events.size() != 1)
			return false;
		const auto spline = settings->phases[0].modules[0].params.get<ParticleSplineMovementSettings>();
		const auto collision = settings->phases[0].modules[1].params.get<ParticleCollisionSettings>();
		return settings->source.renderer.Resolve(world, owner) == FindNamed(world, entities, "Renderer") &&
			settings->source.camera.Resolve(world, owner) == FindNamed(world, entities, "Camera") &&
			spline.spline.Resolve(world, owner) == FindNamed(world, entities, "Curve") &&
			collision.depthCamera.Resolve(world, owner) == FindNamed(world, entities, "Camera") &&
			settings->feedback.events[0].target.Resolve(world, owner) == FindNamed(world, entities, "Child");
	}

	// 保存前のkindSceneも汎用変換でkindPrefabと宣言IDへ変わる
	bool CheckSavedReferences(const nlohmann::json& document, AssetID asset) {

		nlohmann::json settings;
		std::unordered_map<std::string, std::string> declarations;
		for (const auto& entity : document.at("Entities")) {
			const auto& components = entity.at("Components");
			declarations[components.at("Name").at("name").get<std::string>()] = entity.at("LocalFileID").get<std::string>();
			if (components.contains("ParticleSystem") && components.at("Name").at("name") == "Particles")
				settings = components.at("ParticleSystem").at("settings");
		}
		if (settings.empty()) return false;
		const char* paths[]{"/source/renderer", "/source/camera", "/phases/0/modules/0/params/spline",
			"/phases/0/modules/1/params/depthCamera", "/feedback/events/0/target"};
		const char* names[]{"Renderer", "Camera", "Curve", "Camera", "Child"};
		for (uint32_t i = 0; i < 5; ++i) {
			const auto& reference = settings.at(nlohmann::json::json_pointer(paths[i]));
			if (reference.at("kind") != "Prefab" || reference.at("sourceAsset") != ToString(asset) ||
				reference.at("localFileId") != declarations.at(names[i])) return false;
		}
		return true;
	}

	// 実際のPrefab保存、同じSceneへの二重展開、Stage再保存、階層複製を確認する
	bool CheckPrefabReferences() {

		NEMTests::TestDirectory directory("ParticlePrefabReferences", RuntimePaths::GetGameAssetsRoot());
		AssetDatabase database;
		if (!database.Init()) return false;
		ECSWorld authoring;
		HierarchySystem hierarchy;
		PrefabSystem prefabs;
		const auto root = SceneAuthoring::CreateGameObject(authoring, "Particles");
		authoring.AddComponent<ParticleSystemComponent>(root);
		std::vector<Entity> entities{root};
		for (const char* name : {"Renderer", "Camera", "Curve", "Child"}) {
			const auto child = SceneAuthoring::CreateGameObject(authoring, name);
			hierarchy.SetParent(authoring, child, root);
			entities.push_back(child);
		}
		authoring.AddComponent<MeshRendererComponent>(entities[1]);
		authoring.AddComponent<PerspectiveCameraComponent>(entities[2]);
		authoring.AddComponent<SplineComponent>(entities[3]);
		authoring.AddComponent<ParticleSystemComponent>(entities[4]);
		SetReferences(authoring, root, entities);
		if (GetParticleSystemSettings(authoring, root)->source.renderer.prefab) return false;
		const auto path = directory.GetPath() / "Particles.prefab.json";
		const auto logical = RuntimePaths::ToAssetPath(path);
		if (!prefabs.SavePrefab(database, authoring, root, logical)) return false;
		const auto* metadata = database.FindByPath(logical);
		nlohmann::json saved;
		if (!metadata || !JsonFile::TryLoad(path, saved)) return false;
		const auto asset = metadata->guid;
		if (!CheckSavedReferences(saved, asset)) return false;
		ECSWorld world;
		PrefabInstantiateDesc desc;
		desc.ownerSceneInstanceID = Engine::UUID{600};
		PrefabInstantiateResult first, second;
		if (!prefabs.InstantiatePrefab(database, hierarchy, world, asset, first, desc) ||
			!prefabs.InstantiatePrefab(database, hierarchy, world, asset, second, desc)) return false;
		if (!CheckReferences(world, first.root, first.createdEntities) || !CheckReferences(world, second.root, second.createdEntities)) return false;
		const auto firstSettings = GetParticleSystemSettings(world, first.root);
		const auto secondSettings = GetParticleSystemSettings(world, second.root);
		if (firstSettings->source.renderer.localFileID == secondSettings->source.renderer.localFileID) return false;
		// PrefabStageと同じ恒等remapで展開し、FromEntityで再編集してから保存する
		ECSWorld stage;
		std::vector<std::pair<Engine::UUID, Engine::UUID>> identity;
		for (const auto& entity : saved.at("Entities")) {
			const auto local = FromString16Hex(entity.at("LocalFileID").get<std::string>());
			identity.emplace_back(local, local);
		}
		PrefabInstantiateDesc stageDesc;
		stageDesc.ownerSceneInstanceID = Engine::UUID{700};
		stageDesc.localFileIDRemap = &identity;
		stageDesc.preserveNestedLocalFileIDs = true;
		PrefabInstantiateResult editing;
		if (!prefabs.InstantiatePrefab(database, hierarchy, stage, asset, editing, stageDesc)) return false;
		SetReferences(stage, editing.root, editing.createdEntities);
		if (GetParticleSystemSettings(stage, editing.root)->source.renderer.prefab) return false;
		nlohmann::json edited;
		if (!PrefabSnapshotBuilder::BuildDocument(database, stage, editing.root, editing.createdEntities,
			logical, editing.prefabInstanceID, asset, edited) || !CheckSavedReferences(edited, asset)) return false;
		if (!prefabs.SavePrefabFromEntities(database, stage, editing.root, editing.createdEntities, logical, editing.prefabInstanceID))
			return false;
		PrefabInstantiateResult reloaded;
		desc.ownerSceneInstanceID = Engine::UUID{800};
		if (!prefabs.InstantiatePrefab(database, hierarchy, world, asset, reloaded, desc) ||
			!CheckReferences(world, reloaded.root, reloaded.createdEntities)) return false;
		EntityTreeSnapshot snapshot;
		EntitySnapshotUtility::CaptureSubtree(world, first.root, snapshot);
		const std::vector<EntityTreeSnapshot> originals{snapshot};
		const auto duplicated = EntitySnapshotBatchDuplicator::Build(originals);
		const auto restored = EntitySnapshotUtility::RestoreSubtree(world, duplicated.front());
		hierarchy.RebuildRuntimeLinks(world, restored);
		const auto duplicateRoot = world.FindByUUID(duplicated.front().rootStableUUID);
		if (!world.IsAlive(duplicateRoot) || !CheckReferences(world, duplicateRoot, restored)) return false;
		return GetParticleSystemSettings(world, duplicateRoot)->source.renderer.localFileID != firstSettings->source.renderer.localFileID;
	}
}

bool NEMTests::TestParticleSourceReferences() {

	ECSWorld world(ECSWorldKind::Runtime);
	const AssetID asset = ParseAssetID({{"asset", "4e454d5041525449434c45534f55524345"}}, "asset");
	const auto makeEntity = [&](Engine::UUID instance, Engine::UUID local) {
		const Entity entity = world.CreateEntity();
		world.AddComponent<SceneObjectComponent>(entity);
		auto& scene = world.GetComponent<SceneObjectComponent>(entity);
		scene.sourceAsset = asset;
		scene.localFileID = local;
		scene.sceneInstanceID = instance;
		return entity;
	};
	const Entity ownerA = makeEntity(Engine::UUID{1}, Engine::UUID{10});
	const Entity ownerB = makeEntity(Engine::UUID{2}, Engine::UUID{10});
	const Entity targetA = makeEntity(Engine::UUID{1}, Engine::UUID{20});
	const Entity targetB = makeEntity(Engine::UUID{2}, Engine::UUID{20});
	const auto reference = SceneEntityReference::FromEntity(world, targetA);
	if (reference.Resolve(world, ownerA) != targetA || reference.Resolve(world, ownerB) != targetB) return false;

	// 同じSceneの別Instanceへ失われた参照を付け替えない
	world.DestroyEntity(targetA);
	if (world.IsAlive(reference.Resolve(world, ownerA))) return false;
	auto settings = MakeDefaultParticleSystemSettings();
	settings.source.kind = ParticleSourceKind::MeshSurface;
	settings.source.mesh = asset;
	settings.source.renderer = reference;
	settings.source.subMeshes = {1, 3};
	settings.source.inheritColor = true;
	settings.source.attachToSurface = true;
	settings.source.maskMinimum = 0.35f;
	settings.source.maskTexture = AssetID{1, 2};
	settings.surface.normalTexture = AssetID{3, 4};
	AssetDependencyScanner::IDReferences dependencies;
	AssetDependencyScanner::PathReferences paths;
	auto data = ToJson(settings);
	data["field"] = ToString(AssetID{5, 6});
	data["heightmap"] = ToString(AssetID{7, 8});
	AssetDependencyScanner::ScanReferences(data, dependencies, paths);
	for (const AssetID dependency : {AssetID{1, 2}, AssetID{3, 4}, AssetID{5, 6}, AssetID{7, 8}})
		if (!dependencies.contains(dependency)) return false;
	ParticleSystemSettings restored;
	if (!FromJson(ToJson(settings), restored)) return false;
	if (ToJson(settings) != ToJson(restored)) return false;
	if (restored.source.renderer.sourceAsset != asset || restored.source.renderer.localFileID != Engine::UUID{20}) return false;
	return !world.IsAlive(restored.source.renderer.Resolve(world, ownerA)) &&
		restored.source.renderer.Resolve(world, ownerB) == targetB && RunTest("ParticlePrefabReferences", CheckPrefabReferences);
}
