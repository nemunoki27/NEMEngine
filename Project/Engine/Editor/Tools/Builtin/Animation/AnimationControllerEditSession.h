#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Controllers/AnimationControllerAsset.h>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	AnimationControllerEditSession class
	//	Controllerの編集中の定義と保存状態を所有する
	//============================================================================
	class AnimationControllerEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Select(AssetDatabase& database, AssetID assetID);
		bool Save(AssetDatabase& database);
		void MarkModified();

		//--------- accessor -----------------------------------------------------

		AssetID GetAssetID() const { return assetID_; }
		AnimationControllerAsset& GetDraft() { return draft_; }
		bool IsDirty() const { return dirty_; }
		uint64_t GetRevision() const { return revision_; }
		const std::string& GetStatus() const { return status_; }
	private:
		//--------- variables ----------------------------------------------------

		AssetID assetID_{};
		AnimationControllerAsset draft_;
		bool dirty_ = false;
		uint64_t revision_ = 0;
		std::string status_;
	};
}
