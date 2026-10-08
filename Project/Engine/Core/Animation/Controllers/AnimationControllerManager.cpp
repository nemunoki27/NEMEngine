#include "AnimationControllerManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

const Engine::AnimationControllerManager::Definition* Engine::AnimationControllerManager::GetOrLoad(
	AssetDatabase& database, AssetID assetID) {

	if (!assetID) return nullptr;
	const AssetMeta* meta = database.Find(assetID);
	if (!meta || meta->type != AssetType::AnimationController) return nullptr;
	CacheEntry& entry = entries_[assetID];
	const uint64_t contentRevision = database.GetContentRevision(assetID);
	const uint64_t structureRevision = database.GetStructureRevision();
	bool reload = entry.contentRevision != contentRevision;
	if (entry.structureRevision != structureRevision) {

		const auto path = database.ResolveFullPath(assetID);
		reload |= entry.path != path;
		entry.path = path;
		entry.structureRevision = structureRevision;
	}
	if (reload) {

		// 失敗したrevisionは次の更新まで再試行しない
		entry.contentRevision = contentRevision;
		AnimationControllerAsset loaded;
		if (LoadAnimationControllerAsset(entry.path, loaded)) {

			loaded.guid = assetID;
			// 成功した定義だけを新しい世代として公開する
			entry.definition = Definition{ std::move(loaded), nextGeneration_++ };
		}
	}
	return entry.definition ? &*entry.definition : nullptr;
}

void Engine::AnimationControllerManager::Clear() {

	entries_.clear();
}
