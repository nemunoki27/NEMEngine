#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>
#include <Engine/Editor/Core/EditorSceneRequest.h>

// c++
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
			SceneSystem& sceneSystem, EditorManager& editorManager,
			std::function<void()> restoreEditModeUIVisuals);

		// エディタワールドのアクティブシーンを保存する
		bool SaveActiveEditScene();
		// エディタワールドで読み込み中のシーンを全て保存する
		SceneSaveOutcome SaveAllEditScenes(
			const std::unordered_map<AssetID, UUID>& selectedInstances = {});
		// 保存元選択が必要なSceneを取り出す
		std::vector<SceneSaveConflictChoice> ConsumePendingConflicts();
		// 完了した保存と再要求を処理する
		void UpdateSceneSave();
		// 保存と再要求が完了するまで待機する
		bool WaitForSceneSave();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct SceneSaveResult {

			bool succeeded = false;
			std::vector<UUID> matchingInstances;
		};

		struct SceneSaveJob {

			std::future<SceneSaveResult> result;
			AssetID sceneAsset{};
			std::unordered_map<UUID, uint64_t> dirtyRevisions;
			std::string scenePath;
			std::chrono::steady_clock::time_point startedAt{};
		};
		//--------- variables ----------------------------------------------------

		std::optional<SceneSaveJob> sceneSaveJob_;
		std::deque<UUID> queuedInstances_;
		AssetDatabase& assetDatabase_;
		WorldManager& worldManager_;
		SceneInstanceManager& editScenes_;
		SceneSystem& sceneSystem_;
		EditorManager& editorManager_;
		std::function<void()> restoreEditModeUIVisuals_;
		// 保存元選択が必要な競合を次のEditor frameへ渡す
		std::vector<SceneSaveConflictChoice> pendingConflicts_;

		//--------- functions ----------------------------------------------------

		// 保存完了と結果を編集状態へ反映する
		bool FinishSceneSave(bool wait, bool* outSucceeded = nullptr);
		// 要求時点のSceneを保存し、Active切替で保存先を変えない
		bool SaveEditScene(UUID instanceID);
	};
}
