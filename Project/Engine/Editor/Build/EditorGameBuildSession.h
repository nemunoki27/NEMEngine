#pragma once

//============================================================================
//	include
//============================================================================
#include "GameBuildService.h"
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine {

	// 製品ビルド画面の入力値
	struct EditorGameBuildDraft {

		std::string sceneName;
		std::string executableName;
		std::string outputPath;
		bool startupFullscreen = false;
	};

	//============================================================================
	//	EditorGameBuildSession class
	//	製品ビルドの入力とプロセスの寿命を所有する
	//============================================================================
	class EditorGameBuildSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Update();
		void Prepare(const EditorPanelContext& context);
		void Start(const EditorPanelContext& context);
		void ResetStatus();
		void RequestDirectory();
		bool ConsumeOpenPopup();

		//--------- accessor -----------------------------------------------------

		EditorGameBuildDraft& GetDraft() { return draft_; }
		const GameBuildService& GetService() const { return gameBuildService_; }
		const std::vector<std::string>& GetSceneNames() const { return buildSceneNames_; }
		const std::string& GetError() const { return buildError_; }
		bool IsDirectoryDialogOpen() const { return buildDirectoryDialog_.IsOpen(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		GameBuildService gameBuildService_;
		EditorShell::DirectorySelectionDialog buildDirectoryDialog_;
		EditorGameBuildDraft draft_;
		std::vector<std::string> buildSceneNames_;
		std::string buildError_;
		bool requestOpenBuildPopup_ = false;

		//--------- functions ----------------------------------------------------

		// 選択された表示名から起動シーンを解決する
		AssetID ResolveBuildScene() const;
	};
}
