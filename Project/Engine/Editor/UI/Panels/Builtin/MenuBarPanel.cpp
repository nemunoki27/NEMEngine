#include "MenuBarPanel.h"
#include "EditorGraphicsMenu.h"
#include <Engine/Editor/Build/EditorGameBuildMenu.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Tools/Core/EditorToolUI.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <EditorBuildInfo.generated.h>

//============================================================================
//	MenuBarPanel classMethods
//============================================================================
void Engine::MenuBarPanel::Draw(const EditorPanelContext& context) {

	if (!ImGui::BeginMainMenuBar()) {
		return;
	}

	ImGui::SetWindowFontScale(0.8f);

	//============================================================================
	//	製品ビルド
	//============================================================================
	EditorGameBuildMenu::DrawMenu(context, *context.gameBuildSession);

	//============================================================================
	//	編集操作
	//============================================================================
	if (ImGui::BeginMenu("編集補助")) {

		ImGui::SetWindowFontScale(0.8f);

		// それぞれの操作の実行可能かどうかを判定する
		const bool canUndo = !context.IsPlaying() && context.editorState && context.editorState->commandHistory.CanUndo();
		const bool canRedo = !context.IsPlaying() && context.editorState && context.editorState->commandHistory.CanRedo();
		const bool canMutateSelection =
			context.editorState && context.editorState->HasValidSelection(context.GetWorld()) && context.CanEditScene();
		const bool canPaste = context.editorState && context.editorState->HasClipboard() && context.CanEditScene();

		// 操作を戻す
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
			context.host->UndoEditorCommand();
		}
		// 操作をやり直す
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
			context.host->RedoEditorCommand();
		}

		ImGui::Separator();

		// 選択しているエンティティを複製する
		if (ImGui::MenuItem("複製", "Ctrl+D", false, canMutateSelection)) {
			context.host->DuplicateSelection();
		}
		// 選択しているエンティティをクリップボードにコピーする
		if (ImGui::MenuItem("コピー", "Ctrl+C", false, canMutateSelection)) {
			context.host->CopySelectionToClipboard();
		}
		// クリップボードの内容をシーンに貼り付ける
		if (ImGui::MenuItem("コピー済みをペースト", "Ctrl+V", false, canPaste)) {
			context.host->PasteClipboard();
		}

		ImGui::Separator();

		// 選択しているエンティティを削除する
		if (ImGui::MenuItem("削除", "Del", false, canMutateSelection)) {
			context.host->DeleteSelection();
		}
		ImGui::Separator();

		// 複数選択
		ImGui::MenuItem("複数選択", "Shift+左クリック", false, false);

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	エディタウィンドウ表示設定
	//============================================================================
	if (ImGui::BeginMenu("ウィンドウ")) {

		ImGui::SetWindowFontScale(0.8f);

		ImGui::MenuItem("パネルを全て非表示", "Tab+Esc", &context.layoutState->hidePanels);
		ImGui::MenuItem("Play開始時にSceneを保存", nullptr, &context.layoutState->autoSaveScenesOnPlay);
		ImGui::MenuItem("Script Errorで一時停止", nullptr, &context.layoutState->pauseOnScriptError);
		if (context.editorContext && context.editorContext->scriptBuildService && ImGui::BeginMenu("Play中のC#再読み込み")) {

			auto* service = context.editorContext->scriptBuildService;
			const ManagedPlayReloadMode mode = service->GetPlayReloadMode();
			if (ImGui::MenuItem("再コンパイルして継続", nullptr, mode == ManagedPlayReloadMode::RecompileAndContinue)) {
				service->SetPlayReloadMode(ManagedPlayReloadMode::RecompileAndContinue);
			}
			if (ImGui::MenuItem("Stop後に再コンパイル", nullptr, mode == ManagedPlayReloadMode::RecompileAfterStop)) {
				service->SetPlayReloadMode(ManagedPlayReloadMode::RecompileAfterStop);
			}
			if (ImGui::MenuItem("自動Stopして再コンパイル", nullptr, mode == ManagedPlayReloadMode::StopAndRecompile)) {
				service->SetPlayReloadMode(ManagedPlayReloadMode::StopAndRecompile);
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();

		ImGui::MenuItem("Toolbar", nullptr, &context.layoutState->showToolbar);
		ImGui::MenuItem("Hierarchy", nullptr, &context.layoutState->showHierarchy);
		ImGui::MenuItem("Inspector", nullptr, &context.layoutState->showInspector);
		ImGui::MenuItem("Project", nullptr, &context.layoutState->showProject);
		ImGui::MenuItem("Console", nullptr, &context.layoutState->showConsole);
		ImGui::MenuItem("SceneView", nullptr, &context.layoutState->showSceneView);
		ImGui::MenuItem("GameView", nullptr, &context.layoutState->showGameView);

		ImGui::SetWindowFontScale(1.0f);

		ImGui::EndMenu();
	}

	//============================================================================
	//	グラフィックス機能表示/切り替え
	//============================================================================
	if (ImGui::BeginMenu("音声設定")) {
		Audio* audio = Audio::GetInstance();
		bool background = audio->IsPlayInBackgroundEnabled();
		if (ImGui::Checkbox("バックグラウンド再生", &background) && !audio->SetPlayInBackground(background)) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "音声設定を保存できません");
		}
		ImGui::EndMenu();
	}
	EditorGraphicsMenu::Draw(context);

	// グラフィックス設定の右隣にツール起動メニューを置く
	EditorToolUI::DrawMenu(context);

	//============================================================================
	//	エディターレイアウト設定
	//============================================================================
	layoutSession_.DrawMenu(context);

	// 使用中のSDK情報を表示
	ImGui::TextDisabled("SDKバージョン: %s / ビルド構成: %s", EditorBuildInfo::kVersion, EditorBuildInfo::kConfiguration);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndMainMenuBar();
	EditorGameBuildMenu::DrawPopup(context, *context.gameBuildSession);
	layoutSession_.DrawPopup(context);
}
