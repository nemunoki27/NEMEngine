#include "EditorGameBuildMenu.h"

#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <algorithm>

void Engine::EditorGameBuildMenu::DrawMenu(const EditorPanelContext& context, EditorGameBuildSession& session) {

	if (!ImGui::BeginMenu("ビルド")) {
		return;
	}

	ImGui::SetWindowFontScale(0.72f);
	const bool canBuild = context.editorContext && context.editorContext->assetDatabase &&
		!context.IsPlaying() && !session.IsPreparingBuild() && !session.GetService().IsBuilding();
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
	if (!MyGUI::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize, 1000.0f)) {
		return;
	}
	session.AdvanceBuildRequest(context);

	const bool isBuilding = session.IsPreparingBuild() || session.GetService().IsBuilding();
	const bool waitingForSave = session.IsWaitingForSceneSave();
	ImGui::BeginDisabled(isBuilding || waitingForSave);
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
		if (MyGUI::BeginPropertyRow("追加で含めるアセット")) {
			ImGui::BeginDisabled(session.IsDirectoryDialogOpen());
			if (ImGui::Button("ファイルを追加")) session.RequestAdditionalAsset(false);
			ImGui::SameLine();
			if (ImGui::Button("フォルダーを追加")) session.RequestAdditionalAsset(true);
			ImGui::EndDisabled();
			const auto& paths = session.GetDraft().additionalAssets;
			for (size_t i = 0; i < paths.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				const bool remove = ImGui::SmallButton("削除");
				ImGui::SameLine();
				ImGui::TextWrapped("%s", paths[i].c_str());
				ImGui::PopID();
				if (remove) {
					session.RemoveAdditionalAsset(i);
					break;
				}
			}
			MyGUI::EndPropertyRow();
		}
		MyGUI::Checkbox("起動時にフルスクリーン", session.GetDraft().startupFullscreen);
		const ValueEditResult widthResult = MyGUI::DragInt("ゲーム画像の幅", session.GetDraft().gameWidth);
		const ValueEditResult heightResult = MyGUI::DragInt("ゲーム画像の高さ", session.GetDraft().gameHeight);
		if (!widthResult.anyItemActive && !heightResult.anyItemActive) {
			session.SetGameSize(session.GetDraft().gameWidth, session.GetDraft().gameHeight);
		}
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	if (waitingForSave) { ImGui::TextWrapped("Sceneの保存を待っています"); }
	if (session.IsSceneSaveChoiceOpen()) {
		ImGui::TextWrapped("未保存のSceneがあります。保存してからビルドするか、保存済みの内容を使うか選択してください。");
		const float choiceWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if (ImGui::Button("保存してビルド", ImVec2(choiceWidth, 0.0f))) { session.RequestSceneSave(); }
		ImGui::SameLine();
		if (ImGui::Button("保存済み内容でビルド", ImVec2(choiceWidth, 0.0f))) { session.Start(context, false, true); }
	}
	const GameBuildState state = session.GetService().GetState();
	if (isBuilding) {
		ImGui::TextDisabled("ビルド中...");
		ImGui::ProgressBar(session.IsPreparingBuild() ? 0.0f : session.GetService().GetProgress(), ImVec2(-1.0f, 0.0f));
		ImGui::TextWrapped("%s", session.IsPreparingBuild() ? "アセットの参照とビルド対象ファイルを確認しています" :
			session.GetService().GetStatusMessage().c_str());
	} else if (state == GameBuildState::AwaitingConfirmation) {
		ImGui::TextWrapped("参照先の欠損があります。継続すると欠損を含む製品を作成します。");
		ImGui::BeginChild("BuildWarnings", ImVec2(0.0f, 160.0f));
		for (const GameBuildWarning& warning : session.GetService().GetWarnings()) {
			ImGui::TextWrapped("ファイル: %s\n%s", warning.assetPath.c_str(), warning.detail.c_str());
			ImGui::Separator();
		}
		ImGui::EndChild();
		ImGui::BeginDisabled(waitingForSave || session.IsSceneSaveChoiceOpen());
		if (ImGui::Button("欠損を確認してビルドを継続", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			session.Start(context, true);
		}
		ImGui::EndDisabled();

	} else if (state == GameBuildState::Completed) {
		ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "完了しました");
		const std::string outputDirectory =
			Algorithm::PathToUTF8(session.GetService().GetOutputDirectory());
		ImGui::TextWrapped("%s", outputDirectory.c_str());
		const auto& report = session.GetService().GetSizeReport();
		if (!report.empty()) {
			ImGui::BeginChild("BuildSizeReport", ImVec2(0.0f, 180.0f));
			ImGui::TextWrapped("%s", report.c_str());
			ImGui::EndChild();
		}
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
	ImGui::BeginDisabled(isBuilding || waitingForSave || session.IsSceneSaveChoiceOpen() || session.GetSceneNames().empty());
	if (ImGui::Button("ビルド", ImVec2(buttonWidth, 0.0f))) {

		session.Start(context);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(isBuilding || waitingForSave);
	if (ImGui::Button("キャンセル", ImVec2(buttonWidth, 0.0f))) {

		session.ResetStatus();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();

	ImGui::EndPopup();
}
