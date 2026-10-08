#include "AnimationControllerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	AnimationControllerTool classMethods
//============================================================================
void Engine::AnimationControllerTool::OpenEditorTool() {

	openWindow_ = true;
}

bool Engine::AnimationControllerTool::HasPendingEdits() const {

	return session_.IsDirty();
}

void Engine::AnimationControllerTool::RequestResolvePendingEdits() {

	// Editor終了にも切替と同じ確認を使う
	openWindow_ = true;
	pendingClose_ = true;
	resolvingClose_ = true;
	closeResult_ = EditorToolCloseResult::None;
}

Engine::EditorToolCloseResult Engine::AnimationControllerTool::ConsumePendingEditCloseResult() {

	const auto result = closeResult_;
	closeResult_ = EditorToolCloseResult::None;
	return result;
}

void Engine::AnimationControllerTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {

		preview_.End();
		return;
	}
	AssetDatabase& database = *context.toolContext.assetDatabase;
	const bool visible = ImGui::Begin("Animation Controller", &openWindow_);
	if (!openWindow_ && session_.IsDirty()) {

		openWindow_ = true;
		pendingClose_ = true;
	}
	DrawPendingEdits(database);
	if (visible) {

		// Play中は編集用の定義を書き換えない
		ImGui::BeginDisabled(context.IsPlaying() || !context.CanEditScene());
		AssetID selected = session_.GetAssetID();
		if (MyGUI::AssetReferenceField("Controller", selected, &database,
			{ AssetType::AnimationController }, {}).valueChanged) {

			// 未保存の定義は確認してから差し替える
			if (session_.IsDirty()) pendingAsset_ = selected;
			else session_.Select(database, selected);
		}
		if (session_.GetAssetID()) {

			const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (ImGui::Button("保存", ImVec2(width, 0.0f))) session_.Save(database);
			ImGui::SameLine();
			if (ImGui::Button("再読み込み", ImVec2(width, 0.0f))) {

				if (session_.IsDirty()) pendingReload_ = true;
				else session_.Select(database, session_.GetAssetID());
			}
			if (MyGUI::InputText("名前", session_.GetDraft().name).valueChanged) session_.MarkModified();
			DrawStates(database);
			DrawParameters();
			DrawTransitions();
			DrawPreview(context);
		} else ImGui::TextDisabled("ProjectからControllerを選択してください");
		if (!session_.GetStatus().empty()) ImGui::TextWrapped("%s", session_.GetStatus().c_str());
		ImGui::EndDisabled();
	}
	if (!visible || !openWindow_ || context.IsPlaying() || !context.CanEditScene() ||
		previewRevision_ != session_.GetRevision()) preview_.End();
	ImGui::End();
}

void Engine::AnimationControllerTool::EndScenePreview() {

	preview_.End();
}

void Engine::AnimationControllerTool::DrawPendingEdits(AssetDatabase& database) {

	if (!pendingClose_ && !pendingAsset_ && !pendingReload_) return;
	ImGui::OpenPopup("Controllerの未保存編集");
	if (!MyGUI::BeginPopupModal("Controllerの未保存編集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::TextWrapped("%s", "変更を保存しますか？");
	const auto finish = [&]() {

		if (pendingAsset_) session_.Select(database, *pendingAsset_);
		else if (pendingReload_) session_.Select(database, session_.GetAssetID());
		if (pendingClose_) openWindow_ = false;
		pendingAsset_.reset();
		pendingClose_ = false;
		pendingReload_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Accepted;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	};
	if (ImGui::Button("保存") && session_.Save(database)) finish();
	ImGui::SameLine();
	if (ImGui::Button("破棄")) {

		// 読込失敗でも破棄できるよう現在の選択を解除する
		const AssetID current = session_.GetAssetID();
		session_.Select(database, {});
		if (!pendingAsset_) pendingAsset_ = current;
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {

		pendingAsset_.reset();
		pendingClose_ = false;
		pendingReload_ = false;
		if (resolvingClose_) closeResult_ = EditorToolCloseResult::Cancelled;
		resolvingClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	if (!session_.GetStatus().empty()) ImGui::TextWrapped("%s", session_.GetStatus().c_str());
	ImGui::EndPopup();
}
