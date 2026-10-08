#include "PrefabBaseCache.h"

//============================================================================
//	include
//============================================================================
#include "PrefabBaseDocument.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <system_error>

//============================================================================
//	PrefabBaseCache classMethods
//============================================================================
std::shared_ptr<const Engine::PrefabBaseEntities> Engine::PrefabBaseCache::Load(
	AssetDatabase& database, AssetID prefabAsset) {

	const auto path = database.ResolveFullPath(prefabAsset);
	const AssetMeta* meta = database.Find(prefabAsset);
	if (path.empty() || !meta || meta->type != AssetType::Prefab) {
		Clear();
		return {};
	}

	// 保存通知とファイル情報の両方で更新を確認する
	std::error_code error;
	const auto writeTime = std::filesystem::last_write_time(path, error);
	if (error) {
		Clear();
		return {};
	}
	const auto fileSize = std::filesystem::file_size(path, error);
	if (error) {
		Clear();
		return {};
	}
	if (base_ && database_ == &database && assetID_ == prefabAsset && sourcePath_ == path &&
		structureRevision_ == database.GetStructureRevision() && contentRevision_ == database.GetContentRevision(prefabAsset) &&
		writeTime_ == writeTime && fileSize_ == fileSize) {
		return base_;
	}

	// 新しい基準を完成させてから公開する
	auto candidate = PrefabBaseDocument::LoadPrefabBaseEntities(database, prefabAsset);
	if (candidate.empty()) {
		Clear();
		return {};
	}
	const auto afterWriteTime = std::filesystem::last_write_time(path, error);
	if (error || afterWriteTime != writeTime) {
		Clear();
		return {};
	}
	const auto afterFileSize = std::filesystem::file_size(path, error);
	if (error || afterFileSize != fileSize) {
		Clear();
		return {};
	}

	base_ = std::make_shared<const PrefabBaseEntities>(std::move(candidate));
	database_ = &database;
	assetID_ = prefabAsset;
	sourcePath_ = path;
	writeTime_ = writeTime;
	fileSize_ = fileSize;
	structureRevision_ = database.GetStructureRevision();
	contentRevision_ = database.GetContentRevision(prefabAsset);
	return base_;
}

void Engine::PrefabBaseCache::Clear() {

	// 表示側へ渡したSnapshotの寿命は短縮しない
	base_.reset();
	database_ = nullptr;
	assetID_ = {};
	sourcePath_.clear();
	writeTime_ = {};
	fileSize_ = 0;
	structureRevision_ = 0;
	contentRevision_ = 0;
}
