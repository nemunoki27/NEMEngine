#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Scripting/Managed/ManagedProcessRunner.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	GameBuild structures
	//============================================================================
	// ビルド対象のシーン
	struct GameBuildSceneEntry {

		AssetID assetID{};
		std::string assetPath;
		std::string displayName;
	};
	// 製品ビルド設定
	struct GameBuildSettings {

		AssetID startupScene{};
		std::string executableName;
		std::filesystem::path outputRoot;
		bool startupFullscreen = false;
	};
	// 製品ビルドの進行状態
	enum class GameBuildState {

		Idle,
		Building,
		Completed,
		Failed,
	};

	// front
	class AssetDatabase;

	//============================================================================
	//	GameBuildService class
	//	Releaseビルドと製品用アセット配置を非同期プロセスで実行する
	//============================================================================
	class GameBuildService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		GameBuildService() = default;
		~GameBuildService();

		GameBuildService(const GameBuildService&) = delete;
		GameBuildService& operator=(const GameBuildService&) = delete;

		// GameAssets内のシーン一覧を更新
		void RefreshScenes(const AssetDatabase& database);
		// 製品ビルドを開始
		bool Start(const GameBuildSettings& settings, const AssetDatabase& database, std::string& outError);
		// 子プロセスの進行を更新
		void Update();
		// 完了表示を待機状態へ戻す
		void ResetStatus();

		//--------- accessor -----------------------------------------------------

		const std::vector<GameBuildSceneEntry>& GetScenes() const { return scenes_; }
		GameBuildState GetState() const { return state_; }
		bool IsBuilding() const { return state_ == GameBuildState::Building; }
		const std::string& GetStatusMessage() const { return statusMessage_; }
		const std::string& GetFailureDetail() const { return failureDetail_; }
		const std::filesystem::path& GetOutputDirectory() const { return outputDirectory_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ManagedProcessRunner processRunner_;
		std::vector<GameBuildSceneEntry> scenes_;
		GameBuildState state_ = GameBuildState::Idle;
		std::string statusMessage_;
		std::string failureDetail_;
		std::filesystem::path manifestPath_;
		std::filesystem::path outputDirectory_;

		//--------- functions ----------------------------------------------------

		// ビルド設定とアセット一覧を一時マニフェストへ保存
		bool WriteManifest(const GameBuildSettings& settings, const AssetDatabase& database,
			std::filesystem::path& outScriptPath, std::string& outError);
		// 一時マニフェストを削除
		void RemoveManifest();
	};
} // Engine
