#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Build/GameBuildService.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Utility/EditorShell.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	MenuBarPanel class
	//	メニューバーパネル
	//============================================================================
	class MenuBarPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MenuBarPanel() = default;
		~MenuBarPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::string layoutNameBuffer_;
		std::string layoutSaveError_;
		bool requestOpenLayoutSavePopup_ = false;

		// 製品ビルド
		GameBuildService gameBuildService_;
		EditorShell::DirectorySelectionDialog buildDirectoryDialog_;
		std::vector<std::string> buildSceneNames_;
		std::string buildSceneName_;
		std::string buildExecutableName_;
		std::string buildOutputPath_;
		std::string buildError_;
		bool buildStartupFullscreen_ = false;
		bool requestOpenBuildPopup_ = false;

		//--------- functions ----------------------------------------------------

		// 製品ビルドメニューを描画
		void DrawGameBuildMenu(const EditorPanelContext& context);
		// 製品ビルド設定Popupを描画
		void DrawGameBuildPopup(const EditorPanelContext& context);
		// 製品ビルドPopupの初期値を更新
		void PrepareGameBuildPopup(const EditorPanelContext& context);
		// 選択中のシーンIDを取得
		AssetID ResolveBuildScene() const;
		// エディターレイアウト設定メニューを描画
		void DrawEditorLayoutMenu(const EditorPanelContext& context);
		// レイアウト名入力Popupを描画
		void DrawLayoutSavePopup(const EditorPanelContext& context);
	};
} // Engine
