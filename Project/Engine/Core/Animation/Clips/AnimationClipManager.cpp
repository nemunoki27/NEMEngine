#include "AnimationClipManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

//============================================================================
//	AnimationClipManager classMethods
//============================================================================

const Engine::AnimationClipAsset* Engine::AnimationClipManager::GetOrLoad(AssetDatabase& database, AssetID clipID) {

	if (!clipID) return nullptr;
	CacheEntry& entry = loaded_[clipID];
	const uint64_t contentRevision = database.GetContentRevision(clipID);
	const uint64_t structureRevision = database.GetStructureRevision();
	bool reload = !entry.attempted || entry.contentRevision != contentRevision;
	if (entry.structureRevision != structureRevision) {

		const auto path = database.ResolveFullPath(clipID);
		reload |= entry.path != path;
		entry.path = path;
		entry.structureRevision = structureRevision;
	}
	if (reload) {

		entry.attempted = true;
		entry.contentRevision = contentRevision;
		AnimationClipAsset clip;
		// 正常に読めた内容だけを公開する
		if (!entry.path.empty() && LoadAnimationClipAsset(entry.path, clip)) {

			clip.guid = clipID;
			entry.clip = std::move(clip);
			entry.valid = true;
			++revision_;
		}
	}
	return entry.valid ? &entry.clip : nullptr;
}

void Engine::AnimationClipManager::Invalidate(AssetID clipID) {

	const auto found = loaded_.find(clipID);
	if (found != loaded_.end()) found->second.attempted = false;
}

void Engine::AnimationClipManager::Clear() {

	loaded_.clear();
}
