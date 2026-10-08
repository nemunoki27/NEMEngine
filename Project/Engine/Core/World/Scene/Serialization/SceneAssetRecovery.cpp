#include "SceneAssetStorage.h"

//============================================================================
//	include
//============================================================================
#include "SceneStorageFiles.h"
#include "SceneStorageJournal.h"
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>

using namespace Engine::SceneStorageFiles;

//============================================================================
//	SceneAssetStorage classMethods
//============================================================================
std::vector<std::filesystem::path> Engine::SceneAssetStorage::GetRecoveries(bool unfinishedOnly) {

	std::lock_guard lock(storageMutex_);
	return SceneStorageJournal::GetRecoveries(unfinishedOnly);
}

bool Engine::SceneAssetStorage::RecoverInternal(const Path& directory, std::string& error, bool rollingBack) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) {
		error = "シーンの保存・削除・修復処理中です";
		return false;
	}
	return SceneStorageJournal::Recover(directory, error, [&](const Path& target) {
		const auto loaded = loadedAssets_.find(PathKey(target));
		if (!rollingBack && loaded != loadedAssets_.end()) {
			RequireClosed(loaded->second);
		}
		for (AssetID id : protectedScenes_) {
			if (!rollingBack &&
				(Algorithm::PathToUTF8(target).find(ToString(id)) != std::string::npos ||
					(AssetTypeResolver::GuessByPath(target) == AssetType::Scene &&
						std::filesystem::exists(Path(target.wstring() + L".meta")) && ReadSceneID(target) == id))) {
				throw std::runtime_error("読み込み中シーンの復旧はできません");
			}
		}
	});
}

bool Engine::SceneAssetStorage::Commit(const std::vector<SceneStorageChange>& changes, const std::string& label,
	std::string& error, const std::function<void()>& check) {

	return SceneStorageJournal::Commit(
		changes, label, error,
		[this](const Path& directory, std::string& rollbackError) {
			const bool recovered = RecoverInternal(directory, rollbackError, true);
			return recovered;
		},
		check);
}

void Engine::SceneAssetStorage::RequireClosed(AssetID id) {

	if (protectedScenes_.contains(id)) {
		throw std::runtime_error("読み込み中のシーンです、先に閉じるかアンロードしてください");
	}
}

bool Engine::SceneAssetStorage::Recover(const Path& directory, std::string& error) {

	return RecoverInternal(directory, error, false);
}
