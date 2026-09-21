#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>

namespace Engine {

	class AssetDatabase;
	class WorldManager;
	class SceneInstanceManager;
	class SceneSystem;
	class EditorManager;

	//============================================================================
	//	SceneSaveController class
	//	保存ジョブと完了時の編集状態を管理する
	//============================================================================
	class SceneSaveController {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneSaveController(AssetDatabase& assetDatabase, WorldManager& worldManager, SceneInstanceManager& editScenes,
			SceneSystem& sceneSystem, EditorManager& editorManager, const std::string& activeScenePath,
			std::function<void()> restoreEditModeUIVisuals);

		// エディタワールドのアクティブシーンを保存する
		bool SaveActiveEditScene();
		// エディタワールドで読み込み中のシーンを全て保存する
		bool SaveAllEditScenes();
		// 完了した保存と再要求を処理する
		void UpdateSceneSave();
		// 保存と再要求が完了するまで待機する
		bool WaitForSceneSave();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct SceneSaveJob {

			std::future<bool> result;
			AssetID sceneAsset{};
			uint64_t dirtyRevision = 0;
			std::string scenePath;
			std::chrono::steady_clock::time_point startedAt{};
		};
		//--------- variables ----------------------------------------------------

		std::optional<SceneSaveJob> sceneSaveJob_;
		bool sceneSaveQueued_ = false;
		AssetDatabase& assetDatabase_;
		WorldManager& worldManager_;
		SceneInstanceManager& editScenes_;
		SceneSystem& sceneSystem_;
		EditorManager& editorManager_;
		const std::string& activeScenePath_;
		std::function<void()> restoreEditModeUIVisuals_;

		//--------- functions ----------------------------------------------------

		// 保存完了と結果を編集状態へ反映する
		bool FinishSceneSave(bool wait, bool* outSucceeded = nullptr);
	};
}
