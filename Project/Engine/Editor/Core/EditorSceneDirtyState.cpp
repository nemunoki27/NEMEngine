#include "EditorSceneDirtyState.h"

void Engine::EditorSceneDirtyState::MarkSceneSaved(AssetID sceneAsset) {

	dirtySceneAssets_.erase(sceneAsset);
	dirtySceneRevisions_.erase(sceneAsset);
}

void Engine::EditorSceneDirtyState::MarkSceneSaved(AssetID sceneAsset, uint64_t dirtyRevision) {

	if (GetSceneDirtyRevision(sceneAsset) != dirtyRevision) {
		return;
	}
	MarkSceneSaved(sceneAsset);
}

void Engine::EditorSceneDirtyState::MarkAllScenesSaved() {

	dirtySceneAssets_.clear();
	dirtySceneRevisions_.clear();
}

void Engine::EditorSceneDirtyState::ResetSceneDirtyState() {

	MarkAllScenesSaved();
}

bool Engine::EditorSceneDirtyState::IsSceneDirty(AssetID sceneAsset) const {

	return sceneAsset && dirtySceneAssets_.contains(sceneAsset);
}

uint64_t Engine::EditorSceneDirtyState::GetSceneDirtyRevision(AssetID sceneAsset) const {

	const auto it = dirtySceneRevisions_.find(sceneAsset);
	return it != dirtySceneRevisions_.end() ? it->second : 0;
}

void Engine::EditorSceneDirtyState::MarkDirty(AssetID sceneAsset) {

	dirtySceneAssets_.insert(sceneAsset);
	dirtySceneRevisions_[sceneAsset] = ++dirtySceneRevision_;
}
