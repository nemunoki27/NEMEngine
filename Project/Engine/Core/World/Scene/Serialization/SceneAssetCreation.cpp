#include "SceneAssetStorage.h"

//============================================================================
//	include
//============================================================================
#include "SceneAssetCopySnapshot.h"
#include "SceneStorageFiles.h"
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>

// c++
#include <stdexcept>
#include <utility>

using namespace Engine::SceneStorageFiles;

//============================================================================
//	SceneAssetStorage classMethods
//============================================================================
bool Engine::SceneAssetStorage::CreateCopies(const std::vector<SceneAssetCopySnapshot>& copies, std::string& error) {

	error.clear();
	if (copies.empty()) {
		return true;
	}
	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) {
		error = "シーンの保存・削除・修復処理中です";
		return false;
	}
	try {

		// フォルダーの所有を変更前に登録
		struct ActorRoot {

			Path path;
			bool created = false;
		};
		std::vector<ActorRoot> actorRoots;
		actorRoots.reserve(copies.size());
		ScopedCleanup cleanup([&actorRoots]() noexcept {
			// 今回作成した空フォルダーだけを削除
			std::error_code ec;
			for (auto iterator = actorRoots.rbegin(); iterator != actorRoots.rend(); ++iterator) {
				if (iterator->created) {
					std::filesystem::remove(iterator->path, ec);
				}
			}
		});
		std::vector<SceneStorageChange> changes;
		std::unordered_set<AssetID> assetIDs;
		for (const auto& copy : copies) {

			const auto& snapshot = copy.snapshot;
			const Path metaPath(snapshot.scenePath.wstring() + L".meta");
			const Path actorRoot = ResolveActorRoot(snapshot.scenePath, snapshot.sceneAsset);
			// 既存SceneやActorの所有を引き継がない
			if (copy.meta.type != AssetType::Scene || !copy.meta.guid || copy.meta.guid != snapshot.sceneAsset ||
				!assetIDs.insert(copy.meta.guid).second || !IsWritable(snapshot.scenePath) ||
				!std::filesystem::is_directory(snapshot.scenePath.parent_path()) ||
				std::filesystem::exists(snapshot.scenePath) || std::filesystem::exists(metaPath) || actorRoot.empty() ||
				std::filesystem::exists(actorRoot) || !SceneDocument::ValidateSceneFileRoot(snapshot.root) ||
				!SceneDocument::ValidateSerializedLocalFileIDs(snapshot.root)) {
				throw std::runtime_error("シーンの複製先または保存データが不正です");
			}
			// metaも本体と同じ操作で保存
			nlohmann::json metadata;
			if (!AssetMetaStorage::BuildMetaDocument(copy.meta, nlohmann::json::object(), metadata)) {
				throw std::runtime_error("複製先のmetaを作成できません");
			}
			changes.push_back({metaPath, std::move(metadata)});
			std::unordered_set<std::string> used;
			AppendSaveChanges(snapshot, changes, used);
			if (snapshot.useExternalActors) {
				actorRoots.push_back({actorRoot});
			}
		}
		// 準備後に現れたファイルも上書きしない
		for (auto& change : changes) {
			change.createOnly = true;
		}
		for (auto& root : actorRoots) {

			std::filesystem::create_directories(root.path.parent_path());
			root.created = std::filesystem::create_directory(root.path);
			if (!root.created) {
				throw std::runtime_error("複製先のActorフォルダーが既に存在します");
			}
		}
		// バッチ全体の復旧記録を確定してから公開
		return Commit(changes, "シーン複製", error, [&changes] {
			for (const auto& change : changes) {
				if (std::filesystem::exists(change.path)) {
					throw std::runtime_error("シーンの複製先が既に存在します");
				}
			}
		});
	} catch (const std::exception& exception) {

		error = exception.what();
		return false;
	}
}

void Engine::SceneAssetStorage::AppendSaveChanges(
	const SceneSaveSnapshot& snapshot, std::vector<SceneStorageChange>& changes, std::unordered_set<std::string>& used) {

	// 通常保存と複製で同じActor文書を作成
	nlohmann::json root = snapshot.root;
	if (snapshot.useExternalActors) {

		const Path actorRoot = ResolveActorRoot(snapshot.scenePath, snapshot.sceneAsset);
		if (actorRoot.empty()) {
			throw std::runtime_error("Actorの配置先を解決できません");
		}
		auto ids = nlohmann::json::array();
		for (auto actor : root.at("Entities")) {

			const auto id = TryParseUUID16Hex(actor.value("LocalFileID", std::string{}));
			if (!id || !used.insert(ToString(*id) + ".actor.json").second) {
				throw std::runtime_error("保存Actor IDが不正または重複しています");
			}
			actor["SchemaVersion"] = 1;
			ids.push_back(ToString(*id));
			changes.push_back({actorRoot / (ToString(*id) + ".actor.json"), std::move(actor)});
		}
		root.erase("Entities");
		root["ExternalActors"] = std::move(ids);
	} else {

		root.erase("ExternalActors");
	}
	changes.push_back({snapshot.scenePath, std::move(root)});
}
