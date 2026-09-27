#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorSceneRequest.h"

// c++
#include <optional>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	EditorRequestSession class
	//	再生とシーン切替の保留要求を所有する
	//============================================================================
	class EditorRequestSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 操作要求を受け付ける
		void RequestPlayToggle();
		// 操作要求を受け付ける
		void RequestPlayResume();
		// 操作要求を受け付ける
		void RequestPlayPause();
		// 操作要求を受け付ける
		void RequestPlayFrameStep();
		// 操作要求を受け付ける
		void RequestNewScene(bool hasDirtyScenes);
		// 操作要求を受け付ける
		void RequestOpenScene(AssetID sceneAsset, bool hasDirtyScenes);
		// 操作要求を受け付ける
		void RequestSaveScene();
		// 操作要求を受け付ける
		void RequestEnterPrefabEdit(AssetID prefabAsset);
		// 操作要求を受け付ける
		void RequestExitPrefabEdit();
		// 操作要求を受け付ける
		void RequestExitPrefabEditAll();
		// 操作要求を受け付ける
		void RequestTogglePrefabInContext();
		// 操作要求を受け付ける
		void RequestSavePrefab();
		// 操作要求を受け付ける
		void RequestCloseUnsavedScenePopup();
		// 操作要求を取り出す
		EditorUnsavedScenePopupResult ConsumeCloseUnsavedScenePopupResult();
		// 操作要求を取り出す
		bool ConsumePlayToggleRequest();
		// 操作要求を取り出す
		bool ConsumePlayResumeRequest();
		// 操作要求を取り出す
		bool ConsumePlayPauseRequest();
		// 操作要求を取り出す
		bool ConsumePlayFrameStepRequest();
		// 操作要求を取り出す
		EditorSceneRequest ConsumeSceneRequest();
		// 未保存確認を表示する
		void DrawUnsavedScenePopup();
		// 未保存確認を表示する
		void DrawCloseUnsavedScenePopup();
		// 同一Sceneの保存元選択を要求する
		void RequestSceneSaveConflict(const std::vector<SceneSaveConflictChoice>& choices);
		// 保存元選択の結果を取り出す
		std::optional<SceneSaveConflictResult> ConsumeSceneSaveConflictResult();
		// 表示した選択肢と照合して保存元の回答を受け付ける
		bool SubmitSceneSaveConflictResult(SceneSaveConflictResult result);
		// 保存元選択Popupを描画する
		void DrawSceneSaveConflictPopup();
		// 保留中の確認だけを解除する
		void ResetPending();
		// 再生要求だけを解除する
		void ResetPlay();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プレイ/ストップの切り替え要求フラグ
		bool requestTogglePlay_ = false;
		bool requestResumePlay_ = false;
		bool requestPausePlay_ = false;
		bool requestPlayFrameStep_ = false;
		// シーン操作要求
		EditorSceneRequest sceneRequest_{};
		// 未保存確認後に実行するシーン操作要求
		EditorSceneRequest pendingSceneRequest_{};
		// 未保存確認ポップアップを開くか
		bool requestOpenUnsavedPopup_ = false;
		// 終了時の未保存確認ポップアップを開くか
		bool requestOpenCloseUnsavedPopup_ = false;
		// 終了時の未保存確認結果
		EditorUnsavedScenePopupResult closeUnsavedScenePopupResult_ = EditorUnsavedScenePopupResult::None;
		// 同一Scene保存の選択Popup
		bool requestOpenSceneSaveConflictPopup_ = false;
		std::vector<SceneSaveConflictChoice> sceneSaveConflictChoices_;
		std::vector<int> sceneSaveConflictSelection_;
		std::optional<SceneSaveConflictResult> sceneSaveConflictResult_;

		//--------- functions ----------------------------------------------------

		// 未保存確認の要求を処理する
		void QueueSceneRequest(const EditorSceneRequest& request, bool hasDirtyScenes);
		// 未保存確認の要求を処理する
		const char* GetSceneRequestActionName(EditorSceneRequestType type) const;
		// 未保存確認の要求を処理する
		void SubmitPendingSceneRequest(bool saveBeforeSubmit);
	};
}
