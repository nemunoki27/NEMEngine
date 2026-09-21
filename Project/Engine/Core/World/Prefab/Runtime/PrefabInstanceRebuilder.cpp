#include "PrefabInstanceRebuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiator.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceUtility.h>

// c++
#include <unordered_map>

using namespace Engine::PrefabInstanceUtility;

Engine::Entity Engine::PrefabInstanceRebuilder::RebuildInstance(PrefabGenerationContext& context,
	const PrefabInstanceData& data, UUID sceneInstanceID, uint32_t nestedDepth) {

	ECSWorld& world = context.world;
	AssetDatabase& database = context.database;
	HierarchySystem& hierarchySystem = context.hierarchySystem;

	PrefabInstanceData validated;
	if (!FromJson(ToJson(data), validated)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] 不正な対応表の復元を中止します AssetID={} InstanceID={}",
			ToString(data.prefabAsset), ToString(data.instanceID));
		return Entity::Null();
	}
	if (!data.prefabAsset) {
		return Entity::Null();
	}

	// ベースのプレファブを、同一インスタンスIDと保存済みローカルIDの対応付きで展開する
	PrefabInstantiateResult result{};
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = sceneInstanceID;
	desc.forcedInstanceID = data.instanceID;
	desc.localFileIDRemap = &data.entityMap;
	desc.stableUUIDRemap = &data.stableUUIDMap;
	desc.nestedInstanceRemap = &data.nestedInstances;
	desc.removedNestedSlots = &data.removedNestedSlots;
	desc.ownerPrefabInstanceID = data.ownerPrefabInstanceID;
	desc.nestedSlotID = data.nestedSlotID;
	desc.isPrefabAssetNested = data.isPrefabAssetNested;
	desc.nestedDepth = nestedDepth;
	if (!PrefabInstantiator::InstantiatePrefab(context, data.prefabAsset, result, desc)) {
		return Entity::Null();
	}
	const PrefabReferenceRemapper::LocalFileIDMap prefabToSceneLocal =
		BuildPrefabToSceneLocalMap(world, result);

	// プレファブ内ローカルIDから生成済みエンティティを引く
	auto findByTarget = [&](UUID target) -> Entity {
		auto it = result.sourceLocalToEntity.find(target);
		return it != result.sourceLocalToEntity.end() ? it->second : Entity::Null();
		};

	// 削除された実体を破棄する
	for (const UUID& target : data.removedEntities) {

		const Entity entity = findByTarget(target);
		if (world.IsAlive(entity)) {
			world.DestroyEntity(entity);
		}
	}
	world.FlushPendingDestroyEntities();
	// ルートが削除済みのインスタンスは子だけを残さず全て破棄する
	if (!world.IsAlive(result.root)) {

		for (const Entity& entity : result.createdEntities) {
			if (world.IsAlive(entity)) {
				world.DestroyEntity(entity);
			}
		}
		world.FlushPendingDestroyEntities();
		return Entity::Null();
	}

	// 削除されたコンポーネントを外す
	for (const auto& removed : data.removedComponents) {

		const Entity entity = findByTarget(removed.target);
		if (world.IsAlive(entity)) {
			world.RemoveComponentByName(entity, removed.type);
		}
	}
	// 追加されたコンポーネントを足す
	for (const auto& added : data.addedComponents) {

		const Entity entity = findByTarget(added.target);
		if (world.IsAlive(entity)) {
			const UUID sceneLocalFileID = added.type == "SceneObject" ? SceneLocalOf(world, entity) : UUID{};
			nlohmann::json value = added.value;
			PrefabReferenceRemapper::RemapComponent(
				added.type, value, prefabToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
			world.AddComponentFromJson(entity, added.type, value);
			if (added.type == "SceneObject") {
				RestoreSceneObjectRuntimeFields(world, entity, data.prefabAsset, sceneInstanceID, sceneLocalFileID);
			}
		}
	}

	// プロパティ差分を、対象とコンポーネント単位にまとめてから適用する
	std::unordered_map<UUID, std::unordered_map<std::string, nlohmann::json>> builders;
	for (const auto& mod : data.modifications) {

		const Entity entity = findByTarget(mod.target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		// 経路の先頭セグメントがコンポーネント型名、残りがコンポーネント内のリーフ経路
		const size_t slash = mod.path.find('/');
		const std::string type = (slash == std::string::npos) ? mod.path : mod.path.substr(0, slash);
		const std::string leaf = (slash == std::string::npos) ? std::string{} : mod.path.substr(slash + 1);

		auto& typeMap = builders[mod.target];
		auto builderIt = typeMap.find(type);
		if (builderIt == typeMap.end()) {

			nlohmann::json current;
			world.SerializeComponentToJson(entity, type, current);
			builderIt = typeMap.emplace(type, std::move(current)).first;
		}
		nlohmann::json value = mod.value;
		PrefabReferenceRemapper::RemapValue(
			value, mod.path, prefabToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
		PrefabJsonDiff::SetAtPath(builderIt->second, leaf, value);
	}
	for (auto& [target, typeMap] : builders) {

		const Entity entity = findByTarget(target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		for (auto& [type, componentJson] : typeMap) {
			const UUID sceneLocalFileID = type == "SceneObject" ? SceneLocalOf(world, entity) : UUID{};
			world.AddComponentFromJson(entity, type, componentJson);
			if (type == "SceneObject") {
				RestoreSceneObjectRuntimeFields(world, entity, data.prefabAsset, sceneInstanceID, sceneLocalFileID);
			}
		}
	}

	// 追加実体を生成して所属とローカルIDを復元する
	std::vector<Entity> addedEntities;
	for (const auto& added : data.addedEntities) {

		const Entity entity = world.CreateEntity(added.stableUUID);
		if (added.components.is_object()) {
			for (auto it = added.components.begin(); it != added.components.end(); ++it) {
				world.AddComponentFromJson(entity, it.key(), it.value());
			}
		}
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		if (added.sceneLocalFileID) {
			sceneObject.localFileID = added.sceneLocalFileID;
		}
		sceneObject.sceneInstanceID = sceneInstanceID;

		// 親への接続はローカルID経由でリンク再構築に任せる
		if (added.parentSceneLocalFileID) {
			if (!world.HasComponent<HierarchyComponent>(entity)) {
				world.AddComponent<HierarchyComponent>(entity);
			}
			world.GetComponent<HierarchyComponent>(entity).parentLocalFileID = added.parentSceneLocalFileID;
		}
		addedEntities.emplace_back(entity);
	}

	// 兄弟順とインスタンス内の親付け替えを適用する
	for (const auto& hierarchyMod : data.hierarchyModifications) {

		const Entity entity = findByTarget(hierarchyMod.target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			world.AddComponent<HierarchyComponent>(entity);
		}
		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		if (hierarchyMod.hasParentOverride) {

			if (hierarchyMod.externalParentSceneLocalFileID) {
				hierarchy.parentLocalFileID = hierarchyMod.externalParentSceneLocalFileID;
			} else if (hierarchyMod.newParentPrefabLocalFileID) {

				const Entity newParent = findByTarget(hierarchyMod.newParentPrefabLocalFileID);
				hierarchy.parentLocalFileID = SceneLocalOf(world, newParent);
			} else {
				hierarchy.parentLocalFileID = UUID{};
			}
		}
		if (hierarchyMod.hasSiblingOrder) {
			hierarchy.siblingOrder = hierarchyMod.siblingOrder;
		}
	}

	// インスタンスと追加実体のランタイムリンクを再構築する
	std::vector<Entity> linkScope;
	linkScope.reserve(result.createdEntities.size() + addedEntities.size());
	for (const Entity& entity : result.createdEntities) {
		if (world.IsAlive(entity)) {
			linkScope.emplace_back(entity);
		}
	}
	for (const Entity& entity : addedEntities) {
		linkScope.emplace_back(entity);
	}
	hierarchySystem.RebuildRuntimeLinks(world, linkScope);

	// ルートの親状態をシーン保存値へ戻す
	if (world.IsAlive(result.root)) {

		hierarchySystem.SetParent(world, result.root, Entity::Null());
		if (data.rootParentSceneLocalFileID) {

			world.GetComponent<HierarchyComponent>(result.root).parentLocalFileID =
				data.rootParentSceneLocalFileID;
			const Entity parent = FindBySceneLocal(world, sceneInstanceID, data.rootParentSceneLocalFileID);
			if (world.IsAlive(parent)) {
				hierarchySystem.SetParent(world, result.root, parent);
			}
		}
	}

	// メッシュのサブメッシュをmesh実体へ正規化する、差分適用でmeshが変わった場合に必要
	for (const Entity& entity : linkScope) {

		if (world.IsAlive(entity) && world.HasComponent<MeshRendererComponent>(entity)) {
			MeshSubMeshAuthoring::SyncEntity(&database, world, entity, true);
		}
	}
	return result.root;
}

Engine::Entity Engine::PrefabInstanceRebuilder::RebuildInstance(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, const PrefabInstanceData& data, UUID sceneInstanceID,
	uint32_t nestedDepth) {

	PrefabGenerationContext context{ database, hierarchySystem, world };
	return RebuildInstance(context, data, sceneInstanceID, nestedDepth);
}
