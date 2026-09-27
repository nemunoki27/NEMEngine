#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

bool Engine::SceneInstanceManager::LoadSceneTree(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID rootAsset) {

	// 新しいツリーを完成させてから現在の実体を破棄する
	SceneInstanceManager candidate;
	UUID rootInstanceID{};
	if (!candidate.LoadSceneBranch(database, sceneSystem, world, rootAsset,
		UUID{}, UUID{}, {}, rootInstanceID)) {
		candidate.UnloadAll(world);
		return false;
	}
	UnloadAll(world);
	scenes_ = std::move(candidate.scenes_);
	active_ = rootInstanceID;
	singleLoadRequestPending_ = false;
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::SynchronizeSubScenes(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, UUID parentInstanceID) {

	SceneInstance* parent = Find(parentInstanceID);
	if (!parent) {
		return false;
	}
	const std::vector<SubSceneSlotDesc> desiredSlots = parent->header.subScenes;
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	for (const SubSceneSlotDesc& slot : desiredSlots) {
		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SubScene] Slot IDまたは名前が空か重複しています parent={} slot={}",
				ToString(parentInstanceID), slot.slotName);
			return false;
		}
	}

	std::unordered_map<UUID, SceneChildLink> existing;
	for (const SceneChildLink& link : parent->childScenes) {
		existing.emplace(link.slotID, link);
	}

	std::vector<AssetID> ancestors;
	for (const SceneInstance* current = parent; current; current = Find(current->parentInstanceID)) {
		ancestors.emplace_back(current->sceneAsset);
	}
	std::reverse(ancestors.begin(), ancestors.end());

	std::vector<SceneChildLink> nextLinks;
	std::vector<UUID> added;
	std::vector<UUID> removed;
	const UUID previousActive = active_;
	for (const SubSceneSlotDesc& slot : desiredSlots) {

		auto found = existing.find(slot.slotID);
		const SceneInstance* existingChild =
			found == existing.end() ? nullptr : Find(found->second.childInstanceID);
		const UUID existingChildID = existingChild ? existingChild->instanceID : UUID{};
		const AssetID existingChildAsset = existingChild ? existingChild->sceneAsset : AssetID{};
		if (!slot.enabled || !slot.sceneAsset) {
			if (existingChildID) {
				removed.emplace_back(existingChildID);
			}
			if (found != existing.end()) {
				existing.erase(found);
			}
			continue;
		}
		if (existingChildID && existingChildAsset == slot.sceneAsset) {
			SceneChildLink link = found->second;
			link.slotName = slot.slotName;
			nextLinks.emplace_back(std::move(link));
			existing.erase(found);
			continue;
		}

		UUID childInstanceID{};
		if (!LoadSceneBranch(database, sceneSystem, world, slot.sceneAsset,
			parentInstanceID, UUID{}, ancestors, childInstanceID)) {

			// 追加分だけを戻し、既存の子Sceneと選択を維持する
			for (auto it = added.rbegin(); it != added.rend(); ++it) {
				UnloadInternal(world, *it);
			}
			active_ = previousActive;
			return false;
		}
		added.emplace_back(childInstanceID);
		if (existingChildID) {
			removed.emplace_back(existingChildID);
		}
		if (found != existing.end()) {
			existing.erase(found);
		}
		nextLinks.push_back({ slot.slotID, slot.slotName, childInstanceID });
	}

	for (const auto& [slotID, link] : existing) {
		removed.emplace_back(link.childInstanceID);
	}
	// 全ての追加に成功した後で不要な枝を取り除く
	for (UUID instanceID : removed) {
		UnloadInternal(world, instanceID);
	}
	parent = Find(parentInstanceID);
	if (!parent) {
		return false;
	}
	parent->childScenes = std::move(nextLinks);
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::LoadSceneBranch(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset,
	UUID parentInstanceID, UUID forcedInstanceID,
	const std::vector<AssetID>& ancestors, UUID& outInstanceID) {

	outInstanceID = UUID{};
	if (!sceneAsset || std::find(ancestors.begin(), ancestors.end(), sceneAsset) != ancestors.end()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SubScene] Scene参照が循環しています Asset={}", ToString(sceneAsset));
		return false;
	}
	const std::filesystem::path path = database.ResolveFullPath(sceneAsset);
	if (path.empty()) {
		return false;
	}

	SceneInstance instance{};
	instance.instanceID = forcedInstanceID ? forcedInstanceID : UUID::New();
	if (Find(instance.instanceID)) {
		return false;
	}
	instance.parentInstanceID = parentInstanceID;
	instance.sceneAsset = sceneAsset;
	if (!sceneSystem.LoadScene(path, world, &database, sceneAsset, instance.instanceID,
		&instance.header, &instance.createdEntities)) {
		return false;
	}

	const UUID instanceID = instance.instanceID;
	const std::vector<SubSceneSlotDesc> slots = instance.header.subScenes;
	scenes_.emplace_back(std::move(instance));

	std::vector<AssetID> childAncestors = ancestors;
	childAncestors.emplace_back(sceneAsset);
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	for (const SubSceneSlotDesc& slot : slots) {

		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second) {
			UnloadInternal(world, instanceID);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SubScene] Slot IDまたは名前が空か重複しています Asset={} slot={}",
				ToString(sceneAsset), slot.slotName);
			return false;
		}
		if (!slot.enabled || !slot.sceneAsset) {
			continue;
		}

		UUID childInstanceID{};
		if (!LoadSceneBranch(database, sceneSystem, world, slot.sceneAsset,
			instanceID, UUID{}, childAncestors, childInstanceID)) {
			UnloadInternal(world, instanceID);
			return false;
		}
		SceneInstance* current = Find(instanceID);
		if (!current) {
			UnloadInternal(world, childInstanceID);
			return false;
		}
		current->childScenes.push_back({ slot.slotID, slot.slotName, childInstanceID });
	}
	outInstanceID = instanceID;
	return true;
}
