#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
// 再生と保存の要求を所有Sessionへ渡す
void Engine::EditorManager::RequestPlayToggle() {

	requests_.RequestPlayToggle();
}

void Engine::EditorManager::RequestPlayResume() {

	requests_.RequestPlayResume();
}

void Engine::EditorManager::RequestPlayPause() {

	requests_.RequestPlayPause();
}

void Engine::EditorManager::RequestPlayFrameStep() {

	requests_.RequestPlayFrameStep();
}

void Engine::EditorManager::RequestNewScene() {

	requests_.RequestNewScene(HasDirtyScenes());
}

void Engine::EditorManager::RequestOpenScene(AssetID sceneAsset) {

	requests_.RequestOpenScene(sceneAsset, HasDirtyScenes());
}

void Engine::EditorManager::RequestSaveScene() {

	requests_.RequestSaveScene();
}

bool Engine::EditorManager::ConsumeBuildSceneSaveRequest() {

	return gameBuildSession_ && gameBuildSession_->ConsumeSceneSaveRequest();
}

void Engine::EditorManager::CompleteBuildSceneSave(bool success) {

	if (gameBuildSession_) {
		gameBuildSession_->CompleteSceneSave(success);
	}
}

void Engine::EditorManager::RequestEnterPrefabEdit(AssetID prefabAsset) {

	requests_.RequestEnterPrefabEdit(prefabAsset);
}

void Engine::EditorManager::RequestExitPrefabEdit() {

	requests_.RequestExitPrefabEdit();
}

void Engine::EditorManager::RequestExitPrefabEditAll() {

	requests_.RequestExitPrefabEditAll();
}

void Engine::EditorManager::RequestTogglePrefabInContext() {

	requests_.RequestTogglePrefabInContext();
}

void Engine::EditorManager::RequestSavePrefab() {

	requests_.RequestSavePrefab();
}

void Engine::EditorManager::RequestCloseUnsavedScenePopup() {

	requests_.RequestCloseUnsavedScenePopup();
}

Engine::EditorUnsavedScenePopupResult Engine::EditorManager::ConsumeCloseUnsavedScenePopupResult() {

	return requests_.ConsumeCloseUnsavedScenePopupResult();
}

void Engine::EditorManager::RequestSceneSaveConflict(const std::vector<SceneSaveConflictChoice>& choices) {

	requests_.RequestSceneSaveConflict(choices);
}

std::optional<Engine::SceneSaveConflictResult> Engine::EditorManager::ConsumeSceneSaveConflictResult() {

	return requests_.ConsumeSceneSaveConflictResult();
}

bool Engine::EditorManager::ConsumePlayToggleRequest() {

	return requests_.ConsumePlayToggleRequest();
}

bool Engine::EditorManager::ConsumePlayResumeRequest() {

	return requests_.ConsumePlayResumeRequest();
}

bool Engine::EditorManager::ConsumePlayPauseRequest() {

	return requests_.ConsumePlayPauseRequest();
}

bool Engine::EditorManager::ConsumePlayFrameStepRequest() {

	return requests_.ConsumePlayFrameStepRequest();
}

Engine::EditorSceneRequest Engine::EditorManager::ConsumeSceneRequest() {

	return requests_.ConsumeSceneRequest();
}

void Engine::EditorManager::MarkSceneSaved(AssetID sceneAsset) {

	dirtyState_.MarkSceneSaved(sceneAsset);
}

void Engine::EditorManager::MarkSceneSaved(AssetID sceneAsset, uint64_t dirtyRevision, UUID instanceID) {

	dirtyState_.MarkSceneSaved(sceneAsset, dirtyRevision, instanceID);
}

void Engine::EditorManager::MarkSceneInstanceSaved(AssetID sceneAsset, UUID instanceID) {

	dirtyState_.MarkSceneInstanceSaved(sceneAsset, instanceID);
}

void Engine::EditorManager::MarkSceneInstanceDirty(AssetID sceneAsset, UUID instanceID) {

	dirtyState_.MarkDirty(sceneAsset, instanceID);
}

void Engine::EditorManager::MarkAllScenesSaved() {

	dirtyState_.MarkAllScenesSaved();
}

void Engine::EditorManager::ResetSceneDirtyState() {

	dirtyState_.ResetSceneDirtyState();
}

bool Engine::EditorManager::IsSceneDirty(AssetID sceneAsset, UUID instanceID) const {

	return dirtyState_.IsSceneDirty(sceneAsset, instanceID);
}

uint64_t Engine::EditorManager::GetSceneDirtyRevision(AssetID sceneAsset, UUID instanceID) const {

	return dirtyState_.GetSceneDirtyRevision(sceneAsset, instanceID);
}
