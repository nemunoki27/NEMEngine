#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <functional>

namespace Engine {

	class AssetDatabase;
	class WorldManager;
	class SceneInstanceManager;
	class SceneSystem;
	class SystemScheduler;
	struct SystemContext;
	class EditorManager;
	class RuntimeWorldBaker;
	class ManagedScriptBuildService;

	//============================================================================
	//	EditorPlaySession class
	//	プレイ遷移と一時停止の状態を管理する
	//============================================================================
	class EditorPlaySession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorPlaySession(AssetDatabase& assetDatabase, WorldManager& worldManager, SceneInstanceManager& editScenes,
			SceneInstanceManager& playScenes, SceneSystem& sceneSystem, SystemScheduler& scheduler,
			SystemContext& systemContext, EditorManager& editorManager, RuntimeWorldBaker& runtimeWorldBaker,
			ManagedScriptBuildService& scriptBuildService, bool& requestFrameDeltaReset,
			std::function<bool()> isPrefabEditing, std::function<bool()> saveAllEditScenes,
			std::function<void()> refreshActiveWorldContext);

		// プレイモードの切り替え
		void HandlePlayToggle();
		// PlayWorldを破棄してEditへ戻す、StopトグルとPlay中script例外の両方で使う
		void StopPlayWorld();
		// C#のApplication終了要求を安全なフレーム終端で処理する
		bool HandleApplicationQuitRequest();
		// PlayWorldを作成してプレイを開始する
		void StartPlayWorld();
		// Play中の一時停止/再開/コマ送り要求を処理する
		void HandlePlayPauseRequests();
		// このフレームにWorldを進行させるか
		bool ShouldAdvanceActiveWorld() const;

		//--------- accessor -----------------------------------------------------

		bool IsPaused() const { return playPaused_; }
		bool IsFrameStepRequested() const { return playFrameStepRequested_; }
		void FinishFrameStep() { playFrameStepRequested_ = false; }
		void SetJustStarted() { playWorldJustStarted_ = true; }
		// 開始直後の進行抑制を一度だけ消費する
		bool ConsumeJustStarted();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		bool pendingPlayStart_ = false;
		bool playPaused_ = false;
		bool playFrameStepRequested_ = false;
		bool playWorldJustStarted_ = false;
		AssetDatabase& assetDatabase_;
		WorldManager& worldManager_;
		SceneInstanceManager& editScenes_;
		SceneInstanceManager& playScenes_;
		SceneSystem& sceneSystem_;
		SystemScheduler& scheduler_;
		SystemContext& systemContext_;
		EditorManager& editorManager_;
		RuntimeWorldBaker& runtimeWorldBaker_;
		ManagedScriptBuildService& scriptBuildService_;
		bool& requestFrameDeltaReset_;
		std::function<bool()> isPrefabEditing_;
		std::function<bool()> saveAllEditScenes_;
		std::function<void()> refreshActiveWorldContext_;

		//--------- functions ----------------------------------------------------

		// 保留中のPlay開始要求を、build/reload完了に応じて進める
		void ProcessPendingPlayStart();
	};
}
