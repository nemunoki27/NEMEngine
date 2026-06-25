#include "AnimationClipManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

//============================================================================
//	AnimationClipManager classMethods
//============================================================================

const Engine::AnimationClipAsset* Engine::AnimationClipManager::GetOrLoad(AssetDatabase& database, AssetID clipID) {

	if (!clipID) {
		return nullptr;
	}

	// キャッシュ済みならそのまま返す
	auto found = loaded_.find(clipID);
	if (found != loaded_.end()) {
		return &found->second;
	}

	// AssetIDからファイルパスを解決してパースする
	const std::filesystem::path path = database.ResolveFullPath(clipID);
	if (path.empty()) {
		return nullptr;
	}
	AnimationClipAsset clip{};
	if (!LoadAnimationClipAsset(path, clip)) {
		return nullptr;
	}
	// 自身のGUIDが空ならアセットIDで補完する
	if (!clip.guid) {
		clip.guid = clipID;
	}

	auto [it, inserted] = loaded_.emplace(clipID, std::move(clip));
	return &it->second;
}

void Engine::AnimationClipManager::Clear() {

	loaded_.clear();
}
