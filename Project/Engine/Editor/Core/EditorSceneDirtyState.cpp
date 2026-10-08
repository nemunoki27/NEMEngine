#include "EditorSceneDirtyState.h"

#include <algorithm>

void Engine::EditorSceneDirtyState::MarkSceneSaved(AssetID sceneAsset) {

	dirtySceneAssets_.erase(sceneAsset);
	dirtySceneRevisions_.erase(sceneAsset);
}

void Engine::EditorSceneDirtyState::MarkSceneSaved(AssetID sceneAsset, uint64_t dirtyRevision, UUID instanceID) {

	if (GetSceneDirtyRevision(sceneAsset, instanceID) != dirtyRevision) {
		return;
	}
	if (instanceID) {
		MarkSceneInstanceSaved(sceneAsset, instanceID);
	} else {
		MarkSceneSaved(sceneAsset);
	}
}

void Engine::EditorSceneDirtyState::MarkSceneInstanceSaved(AssetID sceneAsset, UUID instanceID) {

	const auto found = dirtySceneRevisions_.find(sceneAsset);
	if (found == dirtySceneRevisions_.end() || !instanceID) {
		return;
	}
	// 同じAssetの別Instanceと所属不明の編集は残す
	found->second.erase(instanceID);
	if (found->second.empty()) {
		MarkSceneSaved(sceneAsset);
	}
}

void Engine::EditorSceneDirtyState::MarkAllScenesSaved() {

	dirtySceneAssets_.clear();
	dirtySceneRevisions_.clear();
}

void Engine::EditorSceneDirtyState::ResetSceneDirtyState() {

	MarkAllScenesSaved();
}

bool Engine::EditorSceneDirtyState::IsSceneDirty(AssetID sceneAsset, UUID instanceID) const {

	return sceneAsset && GetSceneDirtyRevision(sceneAsset, instanceID) != 0;
}

uint64_t Engine::EditorSceneDirtyState::GetSceneDirtyRevision(AssetID sceneAsset, UUID instanceID) const {

	const auto it = dirtySceneRevisions_.find(sceneAsset);
	if (it == dirtySceneRevisions_.end()) {
		return 0;
	}
	uint64_t revision = 0;
	for (const auto& [id, value] : it->second) {
		if (!instanceID || !id || id == instanceID) {
			revision = std::max(revision, value);
		}
	}
	return revision;
}

void Engine::EditorSceneDirtyState::MarkDirty(AssetID sceneAsset, UUID instanceID) {

	dirtySceneAssets_.insert(sceneAsset);
	dirtySceneRevisions_[sceneAsset][instanceID] = ++dirtySceneRevision_;
}
