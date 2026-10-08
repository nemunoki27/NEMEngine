#include "PrefabPromotion.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceUtility.h>

// c++
#include <unordered_map>
#include <unordered_set>

using namespace Engine::PrefabInstanceUtility;

bool Engine::PrefabPromotion::CanPromoteAddedEntitySubtree(
	ECSWorld& world, const Entity& root, UUID instanceID) {

	if (!world.IsAlive(root) || !instanceID || world.HasComponent<PrefabLinkComponent>(root)) {
		return false;
	}
	const Entity parent = ParentOf(world, root);
	if (!world.IsAlive(parent) || !world.HasComponent<PrefabLinkComponent>(parent) ||
		world.GetComponent<PrefabLinkComponent>(parent).prefabInstanceID != instanceID) {
		return false;
	}

	const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
	for (const Entity& entity : subtree) {

		if (!world.HasComponent<SceneObjectComponent>(entity) ||
			world.HasComponent<PrefabLinkComponent>(entity) || !SceneLocalOf(world, entity)) {
			return false;
		}
	}
	return !subtree.empty();
}

bool Engine::PrefabPromotion::PromoteAddedEntitySubtrees(nlohmann::json& prefabFileJson,
	ECSWorld& world, AssetID prefabAsset, UUID instanceID, const std::vector<Entity>& roots) {

	if (!prefabAsset || !instanceID || roots.empty() || !prefabFileJson.is_object() ||
		!prefabFileJson.contains("Entities") || !prefabFileJson["Entities"].is_array()) {
		return false;
	}

	std::vector<Entity> addedEntities;
	std::unordered_set<uint64_t> addedEntityKeys;
	for (const Entity& root : roots) {

		if (!CanPromoteAddedEntitySubtree(world, root, instanceID)) {
			return false;
		}
		const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
		for (const Entity& entity : subtree) {

			const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
			if (addedEntityKeys.insert(key).second) {
				addedEntities.emplace_back(entity);
			}
		}
	}
	if (addedEntities.empty()) {
		return false;
	}

	std::unordered_set<UUID> usedPrefabLocalFileIDs;
	for (const auto& entityJson : prefabFileJson["Entities"]) {

		const std::string localFileID = entityJson.value("LocalFileID", std::string{});
		if (!localFileID.empty()) {
			usedPrefabLocalFileIDs.insert(FromString16Hex(localFileID));
		}
	}

	const std::vector<Entity> instanceEntities = PrefabOverrideUtility::CollectInstanceEntities(world, instanceID);
	PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, instanceEntities);
	std::unordered_map<uint64_t, UUID> promotedLocalFileIDs;
	for (const Entity& entity : addedEntities) {

		const UUID sceneLocalFileID = SceneLocalOf(world, entity);
		UUID prefabLocalFileID = sceneLocalFileID;
		while (!prefabLocalFileID || usedPrefabLocalFileIDs.contains(prefabLocalFileID)) {
			prefabLocalFileID = UUID::New();
		}
		usedPrefabLocalFileIDs.insert(prefabLocalFileID);
		sceneToPrefabLocal[sceneLocalFileID] = prefabLocalFileID;
		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		promotedLocalFileIDs.emplace(key, prefabLocalFileID);
	}

	nlohmann::json promotedEntities = nlohmann::json::array();
	for (const Entity& entity : addedEntities) {

		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		const UUID prefabLocalFileID = promotedLocalFileIDs.at(key);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		components.erase("PrefabLink");
		if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
			components["SceneObject"]["localFileId"] = ToString(prefabLocalFileID);
		}
		PrefabReferenceRemapper::RemapComponents(
			components, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);

		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(prefabLocalFileID);
		entityJson["Components"] = std::move(components);
		promotedEntities.push_back(std::move(entityJson));
	}
	for (auto& entityJson : promotedEntities) {
		prefabFileJson["Entities"].push_back(std::move(entityJson));
	}

	PrefabSystem prefabSystem{};
	for (const Entity& entity : addedEntities) {

		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		prefabSystem.SetPrefabLink(
			world, entity, prefabAsset, promotedLocalFileIDs.at(key), instanceID, false);
	}
	return true;
}
