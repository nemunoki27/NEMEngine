#include "ParticleEffectEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

bool Engine::ParticleEffectEditorTool::HasPendingEdits() const {

	return session_.IsDirty();
}

void Engine::ParticleEffectEditorTool::RequestResolvePendingEdits() {

	// Editor終了にもAsset切替と同じ確認を使う
	openWindow_ = true;
	pendingClose_ = true;
	resolvingClose_ = true;
	closeResult_ = EditorToolCloseResult::None;
}

Engine::EditorToolCloseResult Engine::ParticleEffectEditorTool::ConsumePendingEditCloseResult() {

	const auto result = closeResult_;
	closeResult_ = EditorToolCloseResult::None;
	return result;
}

void Engine::ParticleEffectEditorTool::DrawPendingEdits(const EditorToolContext& context) {

	if (!pendingAsset_ && !pendingClose_ && pendingCreate_.empty()) return;
	pendingConfirmation_ = true;
	ImGui::OpenPopup("Effectの未保存編集");
	if (!MyGUI::BeginPopupModal("Effectの未保存編集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextWrapped("%s", "変更を保存しますか？");
	const auto finish = [&]() {

		if (pendingAsset_ && !session_.LoadEffect(context, *pendingAsset_, statusMessage_)) return;
		if (!pendingCreate_.empty() && !session_.CreateEffect(context, statusMessage_, pendingCreate_)) return;
		if (pendingClose_) openWindow_ = false;
		pendingAsset_.reset();
		pendingCreate_.clear();
		pendingClose_ = false;
		pendingConfirmation_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Accepted;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	};
	if (ImGui::Button("保存")) {
		if (session_.SaveEffect(context, statusMessage_)) finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄")) {
		session_.Discard();
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {
		pendingAsset_.reset();
		pendingCreate_.clear();
		pendingClose_ = false;
		pendingConfirmation_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Cancelled;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	if (!statusMessage_.empty()) ImGui::TextWrapped("%s", statusMessage_.c_str());
	ImGui::EndPopup();
}

void Engine::ParticleEffectEditorTool::DrawHistory(const EditorToolContext& context) {

	const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (focused && context.panelContext && context.panelContext->host) {
		// Effectにフォーカスがある間はSceneのUndoへ渡さない
		context.panelContext->host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::None);
	}
	const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(!session_.CanUndo());
	if (ImGui::Button("Undo", ImVec2(width, 0.0f))) session_.Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!session_.CanRedo());
	if (ImGui::Button("Redo", ImVec2(width, 0.0f))) session_.Redo();
	ImGui::EndDisabled();
	const ImGuiIO& io = ImGui::GetIO();
	if (!focused || io.WantTextInput || ImGui::IsAnyItemActive() || !io.KeyCtrl) return;
	if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (io.KeyShift) session_.Redo();
		else session_.Undo();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) session_.Redo();
	if (ImGui::IsKeyPressed(ImGuiKey_S, false)) session_.SaveEffect(context, statusMessage_);
}
