#include "EditorRequestSession.h"

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace {
	constexpr const char* kUnsavedScenePopupName = "シーン未保存通知";
	constexpr const char* kCloseUnsavedScenePopupName = "シーン未保存通知##CloseApplication";
}

void Engine::EditorRequestSession::RequestPlayToggle() {

	// プレイ要求フラグを立てる
	requestTogglePlay_ = true;
}

void Engine::EditorRequestSession::RequestPlayResume() {

	requestResumePlay_ = true;
}

void Engine::EditorRequestSession::RequestPlayPause() {

	requestPausePlay_ = true;
}

void Engine::EditorRequestSession::RequestPlayFrameStep() {

	requestPlayFrameStep_ = true;
}

void Engine::EditorRequestSession::RequestNewScene(bool hasDirtyScenes) {

	QueueSceneRequest({ EditorSceneRequestType::NewScene, AssetID{} }, hasDirtyScenes);
}

void Engine::EditorRequestSession::RequestOpenScene(AssetID sceneAsset, bool hasDirtyScenes) {

	if (!sceneAsset) {
		return;
	}
	QueueSceneRequest({ EditorSceneRequestType::OpenScene, sceneAsset }, hasDirtyScenes);
}

void Engine::EditorRequestSession::RequestSaveScene() {

	sceneRequest_ = { EditorSceneRequestType::SaveScene, AssetID{} };
}

void Engine::EditorRequestSession::RequestEnterPrefabEdit(AssetID prefabAsset) {

	if (!prefabAsset) {
		return;
	}
	sceneRequest_ = { EditorSceneRequestType::EnterPrefabEdit, prefabAsset };
}

void Engine::EditorRequestSession::RequestExitPrefabEdit() {

	sceneRequest_ = { EditorSceneRequestType::ExitPrefabEdit, AssetID{} };
}

void Engine::EditorRequestSession::RequestExitPrefabEditAll() {

	sceneRequest_ = { EditorSceneRequestType::ExitPrefabEditAll, AssetID{} };
}

void Engine::EditorRequestSession::RequestTogglePrefabInContext() {

	sceneRequest_ = { EditorSceneRequestType::TogglePrefabInContext, AssetID{} };
}

void Engine::EditorRequestSession::RequestSavePrefab() {

	sceneRequest_ = { EditorSceneRequestType::SavePrefab, AssetID{} };
}

void Engine::EditorRequestSession::RequestCloseUnsavedScenePopup() {

	requestOpenCloseUnsavedPopup_ = true;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
}

Engine::EditorUnsavedScenePopupResult Engine::EditorRequestSession::ConsumeCloseUnsavedScenePopupResult() {

	EditorUnsavedScenePopupResult result = closeUnsavedScenePopupResult_;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
	return result;
}

bool Engine::EditorRequestSession::ConsumePlayToggleRequest() {

	const bool requested = requestTogglePlay_;
	requestTogglePlay_ = false;
	return requested;
}

bool Engine::EditorRequestSession::ConsumePlayResumeRequest() {

	const bool requested = requestResumePlay_;
	requestResumePlay_ = false;
	return requested;
}

bool Engine::EditorRequestSession::ConsumePlayPauseRequest() {

	const bool requested = requestPausePlay_;
	requestPausePlay_ = false;
	return requested;
}

bool Engine::EditorRequestSession::ConsumePlayFrameStepRequest() {

	const bool requested = requestPlayFrameStep_;
	requestPlayFrameStep_ = false;
	return requested;
}

Engine::EditorSceneRequest Engine::EditorRequestSession::ConsumeSceneRequest() {

	EditorSceneRequest request = sceneRequest_;
	sceneRequest_ = {};
	return request;
}

void Engine::EditorRequestSession::QueueSceneRequest(const EditorSceneRequest& request, bool hasDirtyScenes) {

	if (request.type == EditorSceneRequestType::NewScene ||
		request.type == EditorSceneRequestType::OpenScene) {

		if (hasDirtyScenes) {

			pendingSceneRequest_ = request;
			requestOpenUnsavedPopup_ = true;
			return;
		}
	}
	sceneRequest_ = request;
}

const char* Engine::EditorRequestSession::GetSceneRequestActionName(EditorSceneRequestType type) const {

	switch (type) {
	case EditorSceneRequestType::NewScene:
		return "新しいシーンを作成する";
	case EditorSceneRequestType::OpenScene:
		return "別のシーンを開く";
	default:
		return "シーンを切り替える";
	}
}

void Engine::EditorRequestSession::SubmitPendingSceneRequest(bool saveBeforeSubmit) {

	if (pendingSceneRequest_.type == EditorSceneRequestType::None) {
		return;
	}

	if (saveBeforeSubmit) {

		switch (pendingSceneRequest_.type) {
		case EditorSceneRequestType::NewScene:
			sceneRequest_ = { EditorSceneRequestType::SaveAndNewScene, AssetID{} };
			break;
		case EditorSceneRequestType::OpenScene:
			sceneRequest_ = { EditorSceneRequestType::SaveAndOpenScene, pendingSceneRequest_.sceneAsset };
			break;
		default:
			sceneRequest_ = pendingSceneRequest_;
			break;
		}
	} else {

		sceneRequest_ = pendingSceneRequest_;
	}
	pendingSceneRequest_ = {};
}

void Engine::EditorRequestSession::DrawUnsavedScenePopup() {

	if (requestOpenUnsavedPopup_) {

		ImGui::OpenPopup(kUnsavedScenePopupName);
		requestOpenUnsavedPopup_ = false;
	}

	if (!ImGui::BeginPopupModal(kUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted("読み込み中のシーンに未保存の変更があります");
	ImGui::Text("%s前に保存しますか？", GetSceneRequestActionName(pendingSceneRequest_.type));
	ImGui::Separator();

	if (ImGui::Button("保存", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(true);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("保存しない", ImVec2(120.0f, 0.0f))) {

		SubmitPendingSceneRequest(false);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {

		pendingSceneRequest_ = {};
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::EditorRequestSession::DrawCloseUnsavedScenePopup() {

	if (requestOpenCloseUnsavedPopup_) {

		ImGui::OpenPopup(kCloseUnsavedScenePopupName);
		requestOpenCloseUnsavedPopup_ = false;
	}

	if (!ImGui::BeginPopupModal(kCloseUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted("読み込み中のシーンに未保存の変更があります");
	ImGui::TextUnformatted("保存しますか？");
	ImGui::Separator();

	if (ImGui::Button("保存", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::Save;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("保存しない", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::DontSave;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {

		closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::Cancel;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::EditorRequestSession::ResetPending() {

	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
}

void Engine::EditorRequestSession::ResetPlay() {

	requestTogglePlay_ = false;
	requestResumePlay_ = false;
	requestPausePlay_ = false;
	requestPlayFrameStep_ = false;
}
