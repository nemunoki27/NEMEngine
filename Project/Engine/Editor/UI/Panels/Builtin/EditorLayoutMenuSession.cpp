#include "EditorLayoutMenuSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	EditorLayoutMenuSession classMethods
//============================================================================

void Engine::EditorLayoutMenuSession::DrawMenu(const EditorPanelContext& context) {

	if (!ImGui::BeginMenu("エディターレイアウト設定")) {
		return;
	}

	ImGui::SetWindowFontScale(0.8f);
	// 保存名の入力を開始
	if (ImGui::MenuItem("現在のレイアウトを保存")) {

		layoutNameBuffer_.clear();
		layoutSaveError_.clear();
		requestOpenLayoutSavePopup_ = true;
	}
	if (!context.host->IsEngineLayoutSaveAvailable() && ImGui::MenuItem("レイアウトインポート")) {
		context.host->RequestImportEditorLayouts();
	}
	if (context.host->IsEngineLayoutSaveAvailable() && ImGui::MenuItem("エンジン共有レイアウトを保存")) {
		context.host->RequestSaveAllEngineLayouts();
	}

	ImGui::Separator();
	// 保存済みのレイアウトを列挙
	const std::string activeLayoutID = context.host->GetActiveEditorLayoutID();
	std::string deleteLayoutID;
	for (const EditorLayoutMenuEntry& entry : context.host->GetEditorLayoutEntries()) {

		ImGui::PushID(entry.layoutID.c_str());
		const bool selected = activeLayoutID == entry.layoutID;
		if (ImGui::MenuItem(entry.displayName.c_str(), nullptr, selected)) {
			context.host->RequestApplyEditorLayout(entry.layoutID);
		}

		if (!entry.defaultLayout && ImGui::BeginPopupContextItem("##LayoutContext", ImGuiPopupFlags_MouseButtonRight)) {

			if (ImGui::MenuItem("削除")) {
				deleteLayoutID = entry.layoutID;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	// 候補の走査後に削除を要求
	if (!deleteLayoutID.empty()) {
		context.host->RequestDeleteEditorLayout(deleteLayoutID);
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();
}

void Engine::EditorLayoutMenuSession::DrawPopup(const EditorPanelContext& context) {

	constexpr const char* popupName = "エディターレイアウトの保存";
	// メニュー終了後に保存画面を開く
	if (requestOpenLayoutSavePopup_) {

		ImGui::OpenPopup(popupName);
		requestOpenLayoutSavePopup_ = false;
	}
	if (!MyGUI::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::SetWindowFontScale(0.8f);

	ImGui::Text("レイアウト名");
	ImGui::Separator();
	TextInputPopupResult inputResult =
		MyGUI::InputTextPopupContent("名前", layoutNameBuffer_, layoutSaveError_.empty() ? nullptr : layoutSaveError_.c_str());
	// 保存に成功した場合だけ画面を閉じる
	if (inputResult.submitted) {

		layoutSaveError_.clear();
		if (context.host->RequestSaveEditorLayout(layoutNameBuffer_, layoutSaveError_)) {
			ImGui::CloseCurrentPopup();
		}
	}
	if (inputResult.canceled) {

		layoutSaveError_.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndPopup();
}
