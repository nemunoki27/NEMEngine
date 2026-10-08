#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <unordered_map>

//============================================================================
//	SceneInstanceManager classMethods
//============================================================================
bool Engine::SceneInstanceManager::SaveActive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world) const {

	const SceneInstance* activeScene = GetActive();
	if (!activeScene || !activeScene->sceneAsset) {
		return false;
	}
	return Save(database, sceneSystem, world, activeScene->sceneAsset, activeScene->instanceID);
}

bool Engine::SceneInstanceManager::Save(AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, AssetID sceneAsset, UUID instanceID) const {

	SceneSaveSnapshot snapshot{};
	if (!CaptureSave(database, sceneSystem, world,
		sceneAsset, snapshot, instanceID)) {
		return false;
	}
	return SceneSystem::WriteSaveSnapshot(std::move(snapshot));
}

bool Engine::SceneInstanceManager::CaptureSave(
	AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, AssetID sceneAsset,
	SceneSaveSnapshot& outSnapshot, UUID instanceID) const {

	// 同じAssetを複数ロードしている場合は保存元を明示する
	const SceneInstance* target = nullptr;
	for (const auto& scene : scenes_) {
		if (scene.sceneAsset != sceneAsset || (instanceID && scene.instanceID != instanceID)) {
			continue;
		}
		if (target) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] 保存するScene Instanceを指定してください Asset={}", ToString(sceneAsset));
			return false;
		}
		target = &scene;
	}
	if (!target || !sceneAsset || target->persistent) {
		return false;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(sceneAsset);
	if (fullPath.empty()) {
		return false;
	}

	const std::vector<Entity> ownedEntities = CollectSceneEntities(world, *target);
	return sceneSystem.CaptureSaveSnapshot(
		fullPath, world, target->header, database,
		outSnapshot, &ownedEntities);
}

bool Engine::SceneInstanceManager::CaptureAllSaves(AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, std::vector<SceneSaveSnapshot>& outSnapshots, std::vector<AssetID>& outConflicts,
	const std::unordered_map<AssetID, UUID>& selectedInstances) const {

	struct Candidate {
		SceneSaveSnapshot snapshot;
		bool selected = false;
		bool conflicting = false;
	};
	outConflicts.clear();
	std::vector<Candidate> candidates;
	std::unordered_map<AssetID, size_t> groups;
	for (const SceneInstance& scene : scenes_) {
		if (!scene.sceneAsset || scene.persistent) {
			continue;
		}
		SceneSaveSnapshot snapshot;
		if (!CaptureSave(database, sceneSystem, world, scene.sceneAsset, snapshot, scene.instanceID)) {
			return false;
		}
		const auto selection = selectedInstances.find(scene.sceneAsset);
		const bool explicitlySelected = selection != selectedInstances.end();
		const bool isSelected = explicitlySelected && selection->second == scene.instanceID;
		const auto [group, inserted] = groups.emplace(scene.sceneAsset, candidates.size());
		if (inserted) {
			candidates.push_back({ std::move(snapshot), !explicitlySelected || isSelected, false });
			continue;
		}
		auto& candidate = candidates[group->second];
		if (explicitlySelected) {
			if (isSelected) {
				candidate.snapshot = std::move(snapshot);
				candidate.selected = true;
			}
		} else if (candidate.snapshot.root != snapshot.root ||
			candidate.snapshot.useExternalActors != snapshot.useExternalActors) {
			candidate.conflicting = true;
		}
	}

	// 保存対象が全て確定するまで呼出元のSnapshotを置き換えない
	for (const auto& candidate : candidates) {
		if (!candidate.selected || candidate.conflicting) {
			outConflicts.emplace_back(candidate.snapshot.sceneAsset);
		}
	}
	for (const auto& selection : selectedInstances) {
		if (!groups.contains(selection.first)) {
			outConflicts.emplace_back(selection.first);
		}
	}
	if (!outConflicts.empty()) {
		return false;
	}
	std::vector<SceneSaveSnapshot> snapshots;
	snapshots.reserve(candidates.size());
	for (auto& candidate : candidates) {
		snapshots.emplace_back(std::move(candidate.snapshot));
	}
	outSnapshots.swap(snapshots);
	return true;
}
