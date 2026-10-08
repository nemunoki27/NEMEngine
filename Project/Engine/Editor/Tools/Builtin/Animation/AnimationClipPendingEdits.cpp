#include "AnimationClipTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	AnimationClipTool pendingMethods
//============================================================================
bool Engine::AnimationClipTool::HasPendingEdits() const {

	return session_.GetClipDirty();
}

void Engine::AnimationClipTool::RequestResolvePendingEdits() {

	openWindow_ = true;
	pendingClose_ = true;
	resolvingClose_ = true;
	closeResult_ = EditorToolCloseResult::None;
}

Engine::EditorToolCloseResult Engine::AnimationClipTool::ConsumePendingEditCloseResult() {

	const auto result = closeResult_;
	closeResult_ = EditorToolCloseResult::None;
	return result;
}

void Engine::AnimationClipTool::EndScenePreview() {

	// 開始したWorldへ戻し、別Worldへ旧値を適用しない
	session_.EndPreviewAndRestore();
}

void Engine::AnimationClipTool::RequestClipSwitch(const EditorToolContext& context, AssetID assetID) {

	if (assetID == session_.GetClipAssetID()) return;
	if (session_.GetClipDirty()) {

		pendingClip_ = assetID;
		return;
	}
	session_.EndPreviewAndRestore();
	session_.GetClipAssetID() = assetID;
	session_.LoadClipFromSelectedAsset(context);
}

void Engine::AnimationClipTool::DrawPendingEdits(const EditorToolContext& context) {

	if (!pendingClose_ && !pendingClip_) return;
	ImGui::OpenPopup("Animation Clipの未保存編集");
	if (!MyGUI::BeginPopupModal("Animation Clipの未保存編集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextWrapped("%s", "変更を保存しますか？");
	const auto finish = [&]() {

		session_.EndPreviewAndRestore();
		if (pendingClip_) {

			session_.GetClipAssetID() = *pendingClip_;
			session_.LoadClipFromSelectedAsset(context);
		}
		if (pendingClose_) openWindow_ = false;
		pendingClip_.reset();
		pendingClose_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Accepted;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	};
	if (ImGui::Button("保存")) {

		session_.SaveClipToSelectedAsset(context);
		if (!session_.GetClipDirty() && session_.GetClipErrorText().empty()) finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄")) {

		session_.RevertClipFromSelectedAsset(context);
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {

		pendingClip_.reset();
		pendingClose_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Cancelled;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	if (!session_.GetClipErrorText().empty()) ImGui::TextWrapped("%s", session_.GetClipErrorText().c_str());
	ImGui::EndPopup();
}
