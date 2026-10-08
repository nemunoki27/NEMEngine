#include "EditorRequestSession.h"

#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	include
//============================================================================
#include <imgui.h>

// c++
#include <algorithm>
#include <utility>

namespace {
	// Scene操作の確認文を選ぶ
	const char* GetSceneRequestActionName(Engine::EditorSceneRequestType type) {

		switch (type) {
		case Engine::EditorSceneRequestType::NewScene:
			return "新しいシーンを作成する";
		case Engine::EditorSceneRequestType::OpenScene:
			return "別のシーンを開く";
		default:
			return "シーンを切り替える";
		}
	}

	constexpr const char* kUnsavedScenePopupName = "シーン未保存通知";
	constexpr const char* kCloseUnsavedScenePopupName = "シーン未保存通知##CloseApplication";
	constexpr const char* kSceneSaveConflictPopupName = "同一Scene保存元の選択";
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

	QueueSceneRequest({EditorSceneRequestType::NewScene, AssetID{}}, hasDirtyScenes);
}

void Engine::EditorRequestSession::RequestOpenScene(AssetID sceneAsset, bool hasDirtyScenes) {

	if (!sceneAsset) {
		return;
	}
	QueueSceneRequest({EditorSceneRequestType::OpenScene, sceneAsset}, hasDirtyScenes);
}

void Engine::EditorRequestSession::RequestSaveScene() {

	sceneRequest_ = {EditorSceneRequestType::SaveScene, AssetID{}};
}

void Engine::EditorRequestSession::RequestEnterPrefabEdit(AssetID prefabAsset) {

	if (!prefabAsset) {
		return;
	}
	sceneRequest_ = {EditorSceneRequestType::EnterPrefabEdit, prefabAsset};
}

void Engine::EditorRequestSession::RequestExitPrefabEdit() {

	sceneRequest_ = {EditorSceneRequestType::ExitPrefabEdit, AssetID{}};
}

void Engine::EditorRequestSession::RequestExitPrefabEditAll() {

	sceneRequest_ = {EditorSceneRequestType::ExitPrefabEditAll, AssetID{}};
}

void Engine::EditorRequestSession::RequestTogglePrefabInContext() {

	sceneRequest_ = {EditorSceneRequestType::TogglePrefabInContext, AssetID{}};
}

void Engine::EditorRequestSession::RequestSavePrefab() {

	sceneRequest_ = {EditorSceneRequestType::SavePrefab, AssetID{}};
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

	if (request.type == EditorSceneRequestType::NewScene || request.type == EditorSceneRequestType::OpenScene) {

		if (hasDirtyScenes) {

			pendingSceneRequest_ = request;
			requestOpenUnsavedPopup_ = true;
			return;
		}
	}
	sceneRequest_ = request;
}

void Engine::EditorRequestSession::SubmitPendingSceneRequest(bool saveBeforeSubmit) {

	if (pendingSceneRequest_.type == EditorSceneRequestType::None) {
		return;
	}

	if (saveBeforeSubmit) {

		switch (pendingSceneRequest_.type) {
		case EditorSceneRequestType::NewScene:
			sceneRequest_ = {EditorSceneRequestType::SaveAndNewScene, AssetID{}};
			break;
		case EditorSceneRequestType::OpenScene:
			sceneRequest_ = {EditorSceneRequestType::SaveAndOpenScene, pendingSceneRequest_.sceneAsset};
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

	if (!MyGUI::BeginPopupModal(kUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextWrapped("%s", "読み込み中のシーンに未保存の変更があります");
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

	if (!MyGUI::BeginPopupModal(kCloseUnsavedScenePopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextWrapped("%s", "読み込み中のシーンに未保存の変更があります");
	ImGui::TextWrapped("%s", "保存しますか？");
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

void Engine::EditorRequestSession::RequestSceneSaveConflict(const std::vector<SceneSaveConflictChoice>& choices) {

	sceneSaveConflictChoices_ = choices;
	sceneSaveConflictSelection_.assign(choices.size(), -1);
	sceneSaveConflictResult_.reset();
	requestOpenSceneSaveConflictPopup_ = true;
}

std::optional<Engine::SceneSaveConflictResult> Engine::EditorRequestSession::ConsumeSceneSaveConflictResult() {

	if (!sceneSaveConflictResult_) {
		return std::nullopt;
	}
	std::optional<SceneSaveConflictResult> result = std::move(sceneSaveConflictResult_);
	sceneSaveConflictResult_.reset();
	return result;
}

void Engine::EditorRequestSession::DrawSceneSaveConflictPopup() {

	if (requestOpenSceneSaveConflictPopup_) {

		ImGui::OpenPopup(kSceneSaveConflictPopupName);
		requestOpenSceneSaveConflictPopup_ = false;
	}

	if (!MyGUI::BeginPopupModal(kSceneSaveConflictPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextWrapped("%s", "同じScene Assetを複数Instanceで編集しています");
	ImGui::TextWrapped("%s", "保存するInstanceを選択してください。選択前はファイルを変更しません。");
	ImGui::TextWrapped("%s", "別の内容のInstanceは未保存のまま残ります。切替・終了時は改めて確認します。");
	ImGui::Separator();

	bool canSave = !sceneSaveConflictChoices_.empty() && sceneSaveConflictSelection_.size() == sceneSaveConflictChoices_.size();
	for (size_t i = 0; i < sceneSaveConflictChoices_.size(); ++i) {

		const SceneSaveConflictChoice& choice = sceneSaveConflictChoices_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("Asset: %s", ToString(choice.sceneAsset).c_str());
		std::string selectedLabel = "Instanceを選択";
		if (sceneSaveConflictSelection_[i] >= 0 &&
			static_cast<size_t>(sceneSaveConflictSelection_[i]) < choice.instanceIDs.size()) {
			selectedLabel = ToString(choice.instanceIDs[sceneSaveConflictSelection_[i]]);
		} else {
			canSave = false;
		}
		if (ImGui::BeginCombo("保存元", selectedLabel.c_str())) {
			for (size_t instanceIndex = 0; instanceIndex < choice.instanceIDs.size(); ++instanceIndex) {

				const bool selected = sceneSaveConflictSelection_[i] == static_cast<int>(instanceIndex);
				const std::string instanceLabel = ToString(choice.instanceIDs[instanceIndex]);
				if (ImGui::Selectable(instanceLabel.c_str(), selected)) {
					sceneSaveConflictSelection_[i] = static_cast<int>(instanceIndex);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();
	}

	if (!canSave) {
		ImGui::TextDisabled("保存元をすべて選択してください");
	}
	if (ImGui::Button("保存", ImVec2(120.0f, 0.0f)) && canSave) {

		std::unordered_map<AssetID, UUID> selectedInstances;
		for (size_t i = 0; i < sceneSaveConflictChoices_.size(); ++i) {
			const auto& choice = sceneSaveConflictChoices_[i];
			selectedInstances.emplace(choice.sceneAsset, choice.instanceIDs[sceneSaveConflictSelection_[i]]);
		}
		if (SubmitSceneSaveConflictResult({false, std::move(selectedInstances)})) {
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {

		SubmitSceneSaveConflictResult({true, {}});
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

bool Engine::EditorRequestSession::SubmitSceneSaveConflictResult(SceneSaveConflictResult result) {

	if (sceneSaveConflictChoices_.empty()) {
		return false;
	}
	if (!result.cancelled) {

		// 全Assetに対し、表示したInstanceを一つずつ選ぶ
		if (result.selectedInstances.size() != sceneSaveConflictChoices_.size()) {
			return false;
		}
		for (const SceneSaveConflictChoice& choice : sceneSaveConflictChoices_) {
			const auto found = result.selectedInstances.find(choice.sceneAsset);
			if (found == result.selectedInstances.end() ||
				std::ranges::find(choice.instanceIDs, found->second) == choice.instanceIDs.end()) {
				return false;
			}
		}
	} else {
		// 取消には保存先を持ち越さない
		result.selectedInstances.clear();
	}
	sceneSaveConflictResult_ = std::move(result);
	sceneSaveConflictChoices_.clear();
	sceneSaveConflictSelection_.clear();
	requestOpenSceneSaveConflictPopup_ = false;
	return true;
}

void Engine::EditorRequestSession::ResetPending() {

	pendingSceneRequest_ = {};
	requestOpenUnsavedPopup_ = false;
	requestOpenCloseUnsavedPopup_ = false;
	closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
	requestOpenSceneSaveConflictPopup_ = false;
	sceneSaveConflictChoices_.clear();
	sceneSaveConflictSelection_.clear();
	sceneSaveConflictResult_.reset();
}

void Engine::EditorRequestSession::ResetPlay() {

	requestTogglePlay_ = false;
	requestResumePlay_ = false;
	requestPausePlay_ = false;
	requestPlayFrameStep_ = false;
}
