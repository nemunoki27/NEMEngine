#pragma once

//============================================================================
//	include
//============================================================================
#include "GameBuildService.h"
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

#include <optional>

namespace Engine {

	// 製品ビルド画面の入力値
	struct EditorGameBuildDraft {

		std::string sceneName;
		std::string executableName;
		std::string outputPath;
		std::vector<std::string> additionalAssets;
		int32_t gameWidth = 1920;
		int32_t gameHeight = 1080;
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
		void Start(const EditorPanelContext& context, bool confirmWarnings = false, bool useSavedFiles = false);
		// Sceneの保存をApplicationの安全地点へ要求する
		void RequestSceneSave();
		bool ConsumeSceneSaveRequest();
		void CompleteSceneSave(bool success);
		void AdvanceBuildRequest(const EditorPanelContext& context);
		void ResetStatus();
		void RequestDirectory();
		void RequestAdditionalAsset(bool directory);
		void RemoveAdditionalAsset(size_t index);
		// 製品画像サイズを画面内へ制限してProject設定へ保存する
		void SetGameSize(int32_t width, int32_t height);
		bool ConsumeOpenPopup();

		//--------- accessor -----------------------------------------------------

		EditorGameBuildDraft& GetDraft() { return draft_; }
		const GameBuildService& GetService() const { return gameBuildService_; }
		const std::vector<std::string>& GetSceneNames() const { return buildSceneNames_; }
		const std::string& GetError() const { return buildError_; }
		bool IsDirectoryDialogOpen() const { return buildDirectoryDialog_.IsOpen() || assetDialog_.IsOpen(); }
		bool IsSceneSaveChoiceOpen() const { return sceneSaveChoice_; }
		bool IsPreparingBuild() const { return pendingSettings_.has_value(); }
		bool IsWaitingForSceneSave() const { return waitingForSceneSave_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		GameBuildService gameBuildService_;
		std::optional<GameBuildSettings> pendingSettings_;
		bool preparationShown_ = false;
		EditorShell::DirectorySelectionDialog buildDirectoryDialog_;
		EditorShell::PathSelectionDialog assetDialog_;
		EditorGameBuildDraft draft_;
		std::vector<std::string> buildSceneNames_;
		std::string buildError_;
		bool requestOpenBuildPopup_ = false;
		bool sceneSaveChoice_ = false;
		bool sceneSaveRequested_ = false;
		bool waitingForSceneSave_ = false;
		bool sceneSaveReady_ = false;
		std::vector<GameBuildWarning> savedConfirmedWarnings_;

		//--------- functions ----------------------------------------------------

		// 選択された表示名から起動シーンを解決する
		AssetID ResolveBuildScene() const;
		void SaveAdditionalAssets(std::vector<std::string> assets);
	};
}
