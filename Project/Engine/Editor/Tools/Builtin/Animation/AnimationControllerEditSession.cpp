#include "AnimationControllerEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

//============================================================================
//	AnimationControllerEditSession classMethods
//============================================================================
bool Engine::AnimationControllerEditSession::Select(AssetDatabase& database, AssetID assetID) {

	AnimationControllerAsset loaded;
	if (assetID) {

		const AssetMeta* meta = database.Find(assetID);
		if (!meta || meta->type != AssetType::AnimationController ||
			!LoadAnimationControllerAsset(database.ResolveFullPath(assetID), loaded)) {

			status_ = "Controllerを読み込めません";
			return false;
		}
	}
	// 読込成功後に編集中の定義を切り替える
	loaded.guid = assetID;
	draft_ = std::move(loaded);
	++revision_;
	assetID_ = assetID;
	dirty_ = false;
	status_.clear();
	return true;
}

bool Engine::AnimationControllerEditSession::Save(AssetDatabase& database) {

	if (!assetID_) return false;
	if (!AnimationControllerEvaluator::Validate(draft_, status_)) return false;
	const auto path = database.ResolveFullPath(assetID_);
	if (path.empty() || !SaveAnimationControllerAsset(path, draft_)) {

		status_ = "Controllerを保存できません";
		return false;
	}
	// 保存した定義を次の再生更新で再読込する
	database.NotifyContentChanged(assetID_);
	dirty_ = false;
	status_ = "保存しました";
	return true;
}

void Engine::AnimationControllerEditSession::MarkModified() {

	dirty_ = true;
	++revision_;
	status_.clear();
}
