#include "SceneAssetStorage.h"

//============================================================================
//	include
//============================================================================
#include "SceneStorageFiles.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <set>
#include <unordered_set>
#include <stdexcept>

using namespace Engine::SceneStorageFiles;

//============================================================================
//	SceneAssetStorage classMethods
//============================================================================
bool Engine::SceneAssetStorage::Delete(
	const Path& path, const AssetDatabase& database, std::string& error, std::span<const Path> additionalPaths) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) {
		error = "シーンの保存・削除・修復処理中です";
		return false;
	}
	try {
		if (!IsWritable(path)) {
			throw std::runtime_error("アセットルートの外側は削除できません");
		}
		// 付随ファイルも退避前に削除範囲を確認
		for (const Path& additional : additionalPaths) {
			if (!IsWritable(additional)) {
				throw std::runtime_error("付随ファイルがアセットルートの外側にあります");
			}
		}
		std::set<Path> files;
		std::set<Path> directories;
		auto collect = [&](const Path& target) {
			if (!std::filesystem::exists(target)) {
				return;
			}
			if (std::filesystem::is_directory(target)) {
				directories.insert(target);
				for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
					if (!IsWritable(entry.path())) {
						throw std::runtime_error("削除範囲外へのリンクがあります");
					}
					if (entry.is_directory()) {
						directories.insert(entry.path());
					} else {
						files.insert(entry.path());
					}
				}
			} else {
				files.insert(target);
			}
		};
		collect(path);
		collect(Path(path.wstring() + L".meta"));
		for (const Path& additional : additionalPaths) {
			collect(additional);
		}
		std::unordered_set<AssetID> deleting;
		for (const auto& file : files) {
			if (AssetTypeResolver::GuessByPath(file) == AssetType::Scene) {
				deleting.insert(ReadSceneID(file));
			}
		}
		// Sceneを含む削除だけProject全体の参照を再検査
		if (!deleting.empty()) {
			AssetDatabase current = database;
			for (const auto& [id, meta] : database.GetAssets()) {
				if (!current.RefreshDependencies(id)) {
					throw std::runtime_error("参照の再検査に失敗したため削除を中止しました: " + meta.assetPath);
				}
			}
			for (AssetID id : deleting) {
				RequireClosed(id);
				for (AssetID referencer : current.FindReferencers(id)) {
					const Path source = database.ResolveFullPath(referencer);
					const bool insideDeletion = std::any_of(
						files.begin(), files.end(), [&](const Path& file) { return PathKey(file) == PathKey(source); });
					if (!insideDeletion && !deleting.contains(referencer)) {
						throw std::runtime_error("シーンが参照されています: " + Algorithm::PathToUTF8(source));
					}
				}
				collect(ResolveActorRoot(database.ResolveFullPath(id), id));
			}
		}
		// 管理フォルダー単体の削除はシーン削除を経由させる
		for (const auto& [id, meta] : database.GetAssets()) {
			if (meta.type != AssetType::Scene || deleting.contains(id)) {
				continue;
			}
			const Path root = ResolveActorRoot(database.ResolveFullPath(id), id);
			for (const Path& file : files) {
				if (IsInside(file, root)) {
					throw std::runtime_error("参照中Actorの直接削除はできません、シーンの修復操作を使用してください");
				}
			}
		}
		std::vector<SceneStorageChange> changes;
		for (const Path& file : files) {
			changes.push_back({file, {}, true});
		}
		if (!Commit(changes, "アセット削除", error)) {
			return false;
		}
		for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
			std::error_code ec;
			std::filesystem::remove(*it, ec);
		}
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
