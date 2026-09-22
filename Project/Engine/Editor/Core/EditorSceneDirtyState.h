#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

#include <unordered_map>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	EditorSceneDirtyState class
	//	シーンの未保存状態と変更世代を所有する
	//============================================================================
	class EditorSceneDirtyState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 指定シーンの未保存状態を解除する
		void MarkSceneSaved(AssetID sceneAsset);
		// 保存開始後に再編集されていなければ未保存状態を解除する
		void MarkSceneSaved(AssetID sceneAsset, uint64_t dirtyRevision);
		// 全シーンの未保存状態を解除する
		void MarkAllScenesSaved();
		// 変更世代の連番を保持して編集状態を初期化する
		void ResetSceneDirtyState();
		// 指定シーンに未保存の変更があるか
		bool IsSceneDirty(AssetID sceneAsset) const;
		// シーンの変更世代を取得する
		uint64_t GetSceneDirtyRevision(AssetID sceneAsset) const;
		// シーンを次の変更世代へ進める
		void MarkDirty(AssetID sceneAsset);

		//--------- accessor -----------------------------------------------------

		bool HasDirtyScenes() const { return !dirtySceneAssets_.empty(); }
		const std::unordered_set<AssetID>& GetDirtySceneAssets() const { return dirtySceneAssets_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 未保存の変更があるシーンアセット
		std::unordered_set<AssetID> dirtySceneAssets_;
		// 非同期保存中の再編集を保存済みにしないためのシーン別変更世代
		std::unordered_map<AssetID, uint64_t> dirtySceneRevisions_;
		uint64_t dirtySceneRevision_ = 0;

	};
}
