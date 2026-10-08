#include "InspectorAssetEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	InspectorAssetEditSession classMethods
//============================================================================

void Engine::InspectorAssetEditSession::TrackDrawer(IAssetInspectorDrawer* drawer, AssetID asset) {

	activeAssetDrawer_ = drawer;
	activeInspectedAsset_ = asset;
}

void Engine::InspectorAssetEditSession::KeepPanelOpen(bool& open) {

	if ((!open || requestResolvePendingEdits_) && HasPendingEdits()) {
		// 確認が終わるまで編集状態を保持する
		open = true;
		pendingCloseAssetEdit_ = true;
	}
}

bool Engine::InspectorAssetEditSession::ResolvePanelClose(const EditorPanelContext& context, bool& open) {

	ResolvePendingAssetEdit(context, pendingCloseAssetEdit_ || requestResolvePendingEdits_);
	if (pendingEditCloseResult_ != EditorPanelCloseResult::Accepted) {
		return false;
	}
	open = false;
	if (!pendingCloseForHost_) {
		pendingEditCloseResult_ = EditorPanelCloseResult::None;
	}
	return true;
}

bool Engine::InspectorAssetEditSession::HasPendingEdits() const {

	return activeAssetDrawer_ && activeAssetDrawer_->HasPendingChanges();
}

void Engine::InspectorAssetEditSession::RequestResolvePendingEdits() {

	requestResolvePendingEdits_ = true;
	pendingCloseForHost_ = true;
	pendingEditCloseResult_ = EditorPanelCloseResult::None;
}

Engine::EditorPanelCloseResult Engine::InspectorAssetEditSession::ConsumePendingEditCloseResult() {

	const EditorPanelCloseResult result = pendingEditCloseResult_;
	pendingEditCloseResult_ = EditorPanelCloseResult::None;
	pendingCloseForHost_ = false;
	return result;
}

bool Engine::InspectorAssetEditSession::ResolvePendingAssetEdit(const EditorPanelContext& context, bool closing) {

	if (!context.editorState || !activeAssetDrawer_ || !activeAssetDrawer_->HasPendingChanges()) {
		if (closing) {
			requestResolvePendingEdits_ = false;
			pendingCloseAssetEdit_ = false;
			pendingEditCloseResult_ = EditorPanelCloseResult::Accepted;
		}
		return true;
	}

	EditorState& state = *context.editorState;
	const char* popupName =
		activeAssetDrawer_->GetAssetType() == AssetType::Mesh ? "Meshインポート設定の確認" : "Textureインポート設定の確認";
	const AssetID selectedAsset = state.selectionKind == EditorSelectionKind::Asset ? state.selectedAsset : AssetID{};
	const bool selectionChanged = state.selectionKind != EditorSelectionKind::Asset || selectedAsset != activeInspectedAsset_;
	if (!closing && !selectionChanged && !pendingAssetEditPrompt_) {
		return true;
	}

	if (!pendingAssetEditPrompt_) {
		pendingAssetEditPrompt_ = true;
		pendingAssetSelection_ = state.selectedAsset;
		pendingSelectionKind_ = state.selectionKind;
		pendingSelectedEntities_ = state.selectedEntities;
		pendingSubMeshIndex_ = state.selectedSubMeshIndex;
		pendingSubMeshStableID_ = state.selectedSubMeshStableID;
		pendingJointEntity_ = state.selectedJointSkinnedEntity;
		pendingJointIndex_ = state.selectedJointIndex;
		pendingCloseAssetEdit_ = closing;
		state.SelectAsset(activeInspectedAsset_);
		ImGui::OpenPopup(popupName);
	}

	if (MyGUI::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

		ImGui::TextWrapped("%s", "未適用のインポート設定があります。");
		if (ImGui::Button("適用") && activeAssetDrawer_->ApplyPendingChanges(context)) {
			CompleteAssetEdit(state);
		}
		ImGui::SameLine();
		if (ImGui::Button("破棄")) {
			activeAssetDrawer_->DiscardPendingChanges();
			CompleteAssetEdit(state);
		}
		ImGui::SameLine();
		if (ImGui::Button("キャンセル")) {
			state.SelectAsset(activeInspectedAsset_);
			pendingAssetSelection_ = {};
			pendingSelectionKind_ = EditorSelectionKind::None;
			pendingSelectedEntities_.clear();
			pendingCloseAssetEdit_ = false;
			pendingAssetEditPrompt_ = false;
			requestResolvePendingEdits_ = false;
			pendingEditCloseResult_ = EditorPanelCloseResult::Cancelled;
			if (!pendingCloseForHost_) {
				pendingEditCloseResult_ = EditorPanelCloseResult::None;
			}
			pendingCloseForHost_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	return false;
}

void Engine::InspectorAssetEditSession::CompleteAssetEdit(EditorState& state) {

	// 終了要求がなければ保留した選択へ移る
	if (pendingCloseAssetEdit_) {
		pendingEditCloseResult_ = EditorPanelCloseResult::Accepted;
	} else {
		ApplyPendingSelection(state);
	}
	activeAssetDrawer_ = nullptr;
	activeInspectedAsset_ = {};
	pendingCloseAssetEdit_ = false;
	pendingAssetEditPrompt_ = false;
	requestResolvePendingEdits_ = false;
	ImGui::CloseCurrentPopup();
}

void Engine::InspectorAssetEditSession::ApplyPendingSelection(EditorState& state) {

	const EditorSelectionKind kind = pendingSelectionKind_;
	const AssetID asset = pendingAssetSelection_;
	const std::vector<Entity> entities = std::move(pendingSelectedEntities_);
	pendingAssetSelection_ = {};
	pendingSelectionKind_ = EditorSelectionKind::None;
	pendingSelectedEntities_.clear();
	if (kind == EditorSelectionKind::Asset) {
		state.SelectAsset(asset);
		return;
	}
	state.SetSelectedEntities(entities);
	state.selectionKind = kind;
	state.selectedSubMeshIndex = pendingSubMeshIndex_;
	state.selectedSubMeshStableID = pendingSubMeshStableID_;
	state.selectedJointSkinnedEntity = pendingJointEntity_;
	state.selectedJointIndex = pendingJointIndex_;
}
