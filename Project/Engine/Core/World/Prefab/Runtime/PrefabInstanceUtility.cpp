#include "PrefabInstanceUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

// c++

namespace Engine::PrefabInstanceUtility {

	void NormalizeSceneObjectForDiff(nlohmann::json& components) {

		if (!components.is_object()) {
			return;
		}
		if (!components.contains("SceneObject") || !components["SceneObject"].is_object()) {
			components["SceneObject"] = nlohmann::json::object();
		}
		nlohmann::json& sceneObject = components["SceneObject"];
		sceneObject.erase("localFileId");
		if (!sceneObject.contains("activeSelf")) {
			sceneObject["activeSelf"] = true;
		}
		if (!sceneObject.contains("tag")) {
			sceneObject["tag"] = "Untagged";
		}
		if (!sceneObject.contains("visibilityLayerMask")) {
			sceneObject["visibilityLayerMask"] = 0xFFFFFFFFu;
		}
	}

	void RestoreSceneObjectRuntimeFields(ECSWorld& world, const Entity& entity,
		AssetID sourceAsset, UUID sceneInstanceID, UUID localFileID) {

		if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
			return;
		}
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		if (localFileID) {
			sceneObject.localFileID = localFileID;
		}
		sceneObject.sourceAsset = sourceAsset;
		sceneObject.sceneInstanceID = sceneInstanceID;
	}

	UUID SceneLocalOf(ECSWorld& world, const Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
			return UUID{};
		}
		return world.GetComponent<SceneObjectComponent>(entity).localFileID;
	}

	Entity ParentOf(ECSWorld& world, const Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			return Entity::Null();
		}
		return world.GetComponent<HierarchyComponent>(entity).parent;
	}

	Entity FindBySceneLocal(ECSWorld& world, UUID sceneInstanceID, UUID localFileID) {

		Entity found = Entity::Null();
		if (!localFileID) {
			return found;
		}
		world.ForEachAliveEntity([&](Entity entity) {

			if (found.IsValid() || !world.HasComponent<SceneObjectComponent>(entity)) {
				return;
			}
			const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (sceneObject.sceneInstanceID == sceneInstanceID && sceneObject.localFileID == localFileID) {
				found = entity;
			}
			});
		return found;
	}

	void CollectAddedSubtree(ECSWorld& world, const Entity& entity, std::vector<PrefabAddedEntity>& out) {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (world.HasComponent<PrefabLinkComponent>(entity)) {

			// 別Prefabや既存Prefab Entityは追加Entityへ展開しない
			return;
		}

		PrefabAddedEntity added{};
		added.stableUUID = world.GetUUID(entity);
		added.sceneLocalFileID = SceneLocalOf(world, entity);
		added.parentSceneLocalFileID = SceneLocalOf(world, ParentOf(world, entity));
		world.SerializeEntityComponents(entity, added.components);
		out.emplace_back(std::move(added));

		// 子も追加実体として再帰収集する
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			return;
		}
		Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
		while (world.IsAlive(child)) {

			const Entity next = world.HasComponent<HierarchyComponent>(child) ?
				world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();
			CollectAddedSubtree(world, child, out);
			child = next;
		}
	}

	PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
		ECSWorld& world, const std::vector<Entity>& instanceEntities) {

		PrefabReferenceRemapper::LocalFileIDMap result;
		for (const Entity& entity : instanceEntities) {

			if (!world.IsAlive(entity) || !world.HasComponent<PrefabLinkComponent>(entity)) {
				continue;
			}
			const UUID sceneLocalFileID = SceneLocalOf(world, entity);
			const UUID prefabLocalFileID = world.GetComponent<PrefabLinkComponent>(entity).prefabLocalFileID;
			if (sceneLocalFileID && prefabLocalFileID) {
				result.emplace(sceneLocalFileID, prefabLocalFileID);
			}
		}
		return result;
	}

	PrefabReferenceRemapper::LocalFileIDMap BuildPrefabToSceneLocalMap(
		ECSWorld& world, const PrefabInstantiateResult& result) {

		PrefabReferenceRemapper::LocalFileIDMap localMap;
		for (const auto& [prefabLocalFileID, entity] : result.sourceLocalToEntity) {

			if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
				continue;
			}
			const UUID sceneLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
			if (prefabLocalFileID && sceneLocalFileID) {
				localMap.emplace(prefabLocalFileID, sceneLocalFileID);
			}
		}
		return localMap;
	}
}
