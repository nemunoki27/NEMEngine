#include "EditorGameBuildMenu.h"

#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <algorithm>

void Engine::EditorGameBuildMenu::DrawMenu(const EditorPanelContext& context, EditorGameBuildSession& session) {

	if (!ImGui::BeginMenu("ビルド")) {
		return;
	}

	ImGui::SetWindowFontScale(0.72f);
	const bool canBuild = context.editorContext && context.editorContext->assetDatabase &&
		!context.IsPlaying() && !session.GetService().IsBuilding();
	if (ImGui::MenuItem("ビルド", nullptr, false, canBuild)) {

		session.Prepare(context);
	}
	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();
}

void Engine::EditorGameBuildMenu::DrawPopup(const EditorPanelContext& context, EditorGameBuildSession& session) {

	constexpr const char* popupName = "ゲームのビルド";
	if (session.ConsumeOpenPopup()) {

		ImGui::OpenPopup(popupName);
	}

	ImGui::SetNextWindowSizeConstraints(ImVec2(1000.0f, 0.0f), ImVec2(1000.0f, FLT_MAX));
	if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const bool isBuilding = session.GetService().IsBuilding();
	ImGui::BeginDisabled(isBuilding);
	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("GameBuildSettings");
		MyGUI::StringCombo("最初のシーン", session.GetDraft().sceneName, session.GetSceneNames(), "<シーンがありません>");
		MyGUI::InputText("Exeの名前", session.GetDraft().executableName);

		if (MyGUI::BeginPropertyRow("出力先")) {

			const float buttonWidth = ImGui::CalcTextSize("参照").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float inputWidth = (std::max)(80.0f,
				ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText("##GameBuildOutputPath", &session.GetDraft().outputPath, ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine();
			ImGui::BeginDisabled(session.IsDirectoryDialogOpen());
			if (ImGui::Button("参照")) {
				session.RequestDirectory();
			}
			ImGui::EndDisabled();
			MyGUI::EndPropertyRow();
		}
		MyGUI::Checkbox("起動時にフルスクリーン", session.GetDraft().startupFullscreen);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	const GameBuildState state = session.GetService().GetState();
	if (state == GameBuildState::Building) {
		ImGui::TextDisabled("ビルド中...");
	} else if (state == GameBuildState::Completed) {
		ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "完了しました");
		const std::string outputDirectory =
			Algorithm::PathToUTF8(session.GetService().GetOutputDirectory());
		ImGui::TextWrapped("%s", outputDirectory.c_str());
	} else if (state == GameBuildState::Failed) {
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "失敗しました");
		if (!session.GetService().GetFailureDetail().empty()) {
			ImGui::TextWrapped("%s", session.GetService().GetFailureDetail().c_str());
		}
	} else if (!session.GetError().empty()) {
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", session.GetError().c_str());
	}

	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
	ImGui::BeginDisabled(isBuilding || session.GetSceneNames().empty());
	if (ImGui::Button("ビルド", ImVec2(buttonWidth, 0.0f))) {

		session.Start(context);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(isBuilding);
	if (ImGui::Button("キャンセル", ImVec2(buttonWidth, 0.0f))) {

		session.ResetStatus();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();

	ImGui::EndPopup();
}
