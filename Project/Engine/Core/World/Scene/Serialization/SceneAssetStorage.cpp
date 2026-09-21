#include "SceneAssetStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneStorageFiles.h>
#include <Engine/Core/World/Scene/Serialization/SceneStorageJournal.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <mutex>
#include <set>
#include <unordered_set>

using namespace Engine::SceneStorageFiles;

namespace {

	using namespace Engine;
	using Path = std::filesystem::path;

	// シーン本体と参照中Actorの内容をまとめて比較する
	std::string Revision(const Path& path, AssetID id) {

		std::string revision = FileRevision(path) + FileRevision(Path(path.wstring() + L".meta"));
		const auto data = JsonAdapter::Load(path, false);
		if (data.contains("ExternalActors") && data["ExternalActors"].is_array()) {
			const Path root = SceneAssetStorage::ResolveActorRoot(path, id);
			for (const auto& actor : data["ExternalActors"]) {
				if (!actor.is_string() || !TryParseUUID16Hex(actor.get<std::string>())) return "invalid";
				revision += FileRevision(root / (actor.get<std::string>() + ".actor.json"));
			}
		}
		return revision;
	}

	// メタファイルから所有GUIDを取得する
	AssetID ReadSceneID(const Path& path) {

		AssetMeta meta;
		if (!AssetDatabase::ReadMetaFile(Path(path.wstring() + L".meta"), meta) || !meta.guid) {
			throw std::runtime_error("シーンのメタデータを読めません: " + Algorithm::PathToUTF8(path));
		}
		return meta.guid;
	}

	// 参照空間と所有シーンが一致するEntityRefだけを空にする
	bool ClearActorReferences(nlohmann::json& value, AssetID owner, AssetID targetScene, const std::string& actorID) {

		if (value.is_object()) {
			if (value.contains("kind") && value.contains("sourceAsset") && value.contains("localFileId")) {
				const std::string source = value.value("sourceAsset", std::string{});
				if (value.value("kind", std::string{}) == "Scene" && value.value("localFileId", std::string{}) == actorID &&
					(source == ToString(targetScene) || (source.empty() && owner == targetScene))) {
					value["kind"] = "Null";
					value["sourceAsset"] = "";
					value["localFileId"] = "";
				}
				return true;
			}
			for (auto& child : value.items()) {
				if (!ClearActorReferences(child.value(), owner, targetScene, actorID)) return false;
			}
		} else if (value.is_array()) {
			for (auto& child : value) if (!ClearActorReferences(child, owner, targetScene, actorID)) return false;
		} else if (owner == targetScene && value.is_string() && value.get<std::string>() == actorID) {
			return false;
		}
		return true;
	}

}

std::filesystem::path Engine::SceneAssetStorage::ResolveActorRoot(const Path& scenePath, AssetID sceneAsset) {

	if (!sceneAsset) return {};
	std::vector<Path> roots{ RuntimePaths::GetGameAssetsRoot(), RuntimePaths::GetEngineAssetsRoot() };
	for (const auto& package : RuntimePaths::GetPackages()) roots.push_back(package.root);
	for (const Path& root : roots) {
		if (IsInside(scenePath, root)) return root / "ExternalActors" / ToString(sceneAsset);
	}
	return {};
}

std::vector<Engine::SceneStorageIssue> Engine::SceneAssetStorage::Validate(const Path& scenePath, AssetID sceneAsset) {

	std::lock_guard lock(storageMutex_);
	std::vector<SceneStorageIssue> issues;
	try {
		const auto scene = JsonAdapter::Load(scenePath, false);
		if (!scene.is_object()) throw std::runtime_error("シーン本体が存在しないか不正です");
		if (!scene.contains("ExternalActors")) return issues;
		if (!scene["ExternalActors"].is_array()) throw std::runtime_error("ExternalActorsの一覧が不正です");
		const Path root = ResolveActorRoot(scenePath, sceneAsset);
		if (root.empty()) throw std::runtime_error("Actorの配置先を解決できません");
		std::unordered_set<UUID> ids;
		for (const auto& value : scene["ExternalActors"]) {
			const auto id = value.is_string() ? TryParseUUID16Hex(value.get<std::string>()) : std::nullopt;
			if (!id || !ids.insert(*id).second) {
				issues.push_back({ scenePath, {}, {}, "Actor IDが不正または重複しています" });
				continue;
			}
			const Path path = root / (ToString(*id) + ".actor.json");
			if (!IsInside(path, root)) throw std::runtime_error("Actorが所有フォルダーの外を参照しています");
			const bool missing = !std::filesystem::is_regular_file(path);
			const auto actor = JsonAdapter::Load(path, false);
			if (missing || !actor.is_object() || actor.value("SchemaVersion", 0u) != 1 ||
				actor.value("LocalFileID", std::string{}) != ToString(*id) ||
				!actor.contains("Components") || !actor["Components"].is_object()) {
				issues.push_back({ scenePath, path, *id, missing ? "ExternalActorが見つかりません" : "ExternalActorの内容が不正です", missing });
			}
		}
	} catch (const std::exception& exception) {
		issues.push_back({ scenePath, {}, {}, exception.what() });
	}
	return issues;
}

std::vector<Engine::SceneStorageIssue> Engine::SceneAssetStorage::Inspect(const AssetDatabase& database) {

	std::lock_guard lock(storageMutex_);
	std::vector<SceneStorageIssue> issues;
	std::unordered_set<std::string> roots;
	for (const auto& [id, meta] : database.GetAssets()) {
		if (meta.type != AssetType::Scene) continue;
		const Path path = database.ResolveFullPath(id);
		const Path actorRoot = ResolveActorRoot(path, id);
		if (!actorRoot.empty() && std::filesystem::exists(path)) roots.insert(PathKey(actorRoot));
		auto sceneIssues = Validate(path, id);
		issues.insert(issues.end(), sceneIssues.begin(), sceneIssues.end());
	}
	for (const Path& assetRoot : { RuntimePaths::GetGameAssetsRoot(), RuntimePaths::GetEngineAssetsRoot() }) {
		std::error_code ec;
		for (std::filesystem::directory_iterator it(assetRoot / "ExternalActors", ec), end; !ec && it != end; it.increment(ec)) {
			if (it->is_directory() && !roots.contains(PathKey(it->path()))) {
				issues.push_back({ {}, it->path(), {}, "所有シーンのないActorフォルダーです、復元元を確認してください" });
			}
		}
	}
	return issues;
}

void Engine::SceneAssetStorage::TrackLoaded(const Path& scenePath, AssetID sceneAsset) {

	std::lock_guard lock(storageMutex_);
	loadedRevisions_[PathKey(scenePath)] = Revision(scenePath, sceneAsset);
	loadedAssets_[PathKey(scenePath)] = sceneAsset;
}

void Engine::SceneAssetStorage::SetProtectedScenes(const std::vector<AssetID>& sceneAssets) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) return;
	protectedScenes_ = { sceneAssets.begin(), sceneAssets.end() };
}

bool Engine::SceneAssetStorage::Save(SceneSaveSnapshot snapshot, std::string& error) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) { error = "シーンの保存・削除・修復処理中です"; return false; }
	try {
		const auto tracked = loadedRevisions_.find(PathKey(snapshot.scenePath));
		const std::string originalRevision = Revision(snapshot.scenePath, snapshot.sceneAsset);
		if (tracked != loadedRevisions_.end() && tracked->second != originalRevision) {
			throw std::runtime_error("シーンまたはActorが外部で変更・削除されています、再読み込みまたは修復してください");
		}
		if (std::filesystem::exists(snapshot.scenePath)) {
			const auto issues = Validate(snapshot.scenePath, snapshot.sceneAsset);
			if (!issues.empty()) throw std::runtime_error(issues.front().detail);
		}
		std::vector<SceneStorageChange> changes;
		std::set<std::string> used;
		const Path actorRoot = ResolveActorRoot(snapshot.scenePath, snapshot.sceneAsset);
		if (snapshot.useExternalActors) {
			if (actorRoot.empty()) throw std::runtime_error("Actorの配置先を解決できません");
			auto ids = nlohmann::json::array();
			for (auto actor : snapshot.root.at("Entities")) {
				const auto id = TryParseUUID16Hex(actor.value("LocalFileID", std::string{}));
				if (!id || !used.insert(ToString(*id) + ".actor.json").second) throw std::runtime_error("保存Actor IDが不正または重複しています");
				actor["SchemaVersion"] = 1;
				ids.push_back(ToString(*id));
				changes.push_back({ actorRoot / (ToString(*id) + ".actor.json"), std::move(actor) });
			}
			snapshot.root.erase("Entities");
			snapshot.root["ExternalActors"] = std::move(ids);
		} else {
			snapshot.root.erase("ExternalActors");
		}
		changes.push_back({ snapshot.scenePath, snapshot.root });
		std::error_code ec;
		if (!actorRoot.empty() && std::filesystem::exists(actorRoot)) {
			for (const auto& entry : std::filesystem::directory_iterator(actorRoot)) {
				if (entry.is_regular_file() && entry.path().filename().string().ends_with(".actor.json") &&
					!used.contains(entry.path().filename().string())) changes.push_back({ entry.path(), {}, true });
			}
		}
		if (Revision(snapshot.scenePath, snapshot.sceneAsset) != originalRevision) throw std::runtime_error("保存準備中に外部変更を検出しました");
		if (!Commit(changes, "シーン保存", error)) return false;
		loadedRevisions_[PathKey(snapshot.scenePath)] = Revision(snapshot.scenePath, snapshot.sceneAsset);
		loadedAssets_[PathKey(snapshot.scenePath)] = snapshot.sceneAsset;
		if (!actorRoot.empty()) std::filesystem::remove(actorRoot, ec);
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool Engine::SceneAssetStorage::Delete(const Path& path, const AssetDatabase& database, std::string& error) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) { error = "シーンの保存・削除・修復処理中です"; return false; }
	try {
		if (!IsWritable(path)) throw std::runtime_error("アセットルートの外側は削除できません");
		AssetDatabase current = database;
		for (const auto& [id, meta] : database.GetAssets()) current.RefreshDependencies(id);
		std::set<Path> files;
		std::set<Path> directories;
		auto collect = [&](const Path& target) {
			if (!std::filesystem::exists(target)) return;
			if (std::filesystem::is_directory(target)) {
				directories.insert(target);
				for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
					if (!IsWritable(entry.path())) throw std::runtime_error("削除範囲外へのリンクがあります");
					if (entry.is_directory()) directories.insert(entry.path());
					else files.insert(entry.path());
				}
			} else files.insert(target);
		};
		collect(path);
		collect(Path(path.wstring() + L".meta"));
		std::unordered_set<AssetID> deleting;
		for (const auto& file : files) {
			if (AssetTypeResolver::GuessByPath(file) == AssetType::Scene) deleting.insert(ReadSceneID(file));
		}
		for (AssetID id : deleting) {
			RequireClosed(id);
			for (AssetID referencer : current.FindReferencers(id)) {
				const Path source = database.ResolveFullPath(referencer);
				const bool insideDeletion = std::any_of(files.begin(), files.end(), [&](const Path& file) { return PathKey(file) == PathKey(source); });
				if (!insideDeletion && !deleting.contains(referencer)) {
					throw std::runtime_error("シーンが参照されています: " + Algorithm::PathToUTF8(source));
				}
			}
			collect(ResolveActorRoot(database.ResolveFullPath(id), id));
		}
		// 管理フォルダー単体の削除はシーン削除を経由させる
		for (const auto& [id, meta] : database.GetAssets()) {
			if (meta.type != AssetType::Scene || deleting.contains(id)) continue;
			const Path root = ResolveActorRoot(database.ResolveFullPath(id), id);
			for (const Path& file : files) {
				if (IsInside(file, root)) throw std::runtime_error("参照中Actorの直接削除はできません、シーンの修復操作を使用してください");
			}
		}
		std::vector<SceneStorageChange> changes;
		for (const Path& file : files) changes.push_back({ file, {}, true });
		if (!Commit(changes, "アセット削除", error)) return false;
		for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
			std::error_code ec;
			std::filesystem::remove(*it, ec);
		}
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool Engine::SceneAssetStorage::RestoreActor(const Path& scenePath, UUID actorID, const Path& source, std::string& error) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) { error = "シーンの保存・削除・修復処理中です"; return false; }
	try {
		const AssetID id = ReadSceneID(scenePath);
		RequireClosed(id);
		const auto scene = JsonAdapter::Load(scenePath, false);
		const auto& ids = scene.at("ExternalActors");
		if (std::find(ids.begin(), ids.end(), ToString(actorID)) == ids.end()) throw std::runtime_error("シーンにないActor IDです");
		const auto actor = JsonAdapter::Load(source, false);
		if (actor.value("SchemaVersion", 0u) != 1 || actor.value("LocalFileID", std::string{}) != ToString(actorID) ||
			!actor.contains("Components") || !actor["Components"].is_object()) throw std::runtime_error("復元元のActor IDまたは形式が一致しません");
		const Path target = ResolveActorRoot(scenePath, id) / (ToString(actorID) + ".actor.json");
		if (std::filesystem::exists(target)) throw std::runtime_error("復元先にファイルが存在します、上書きは行いません");
		if (!Commit({ { target, actor } }, "Actor復元", error)) return false;
		loadedRevisions_.erase(PathKey(scenePath));
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool Engine::SceneAssetStorage::RemoveMissingActor(const Path& scenePath, UUID actorID, std::string& error) {

	return UpdateMissingActor(scenePath, actorID, error, nullptr);
}

bool Engine::SceneAssetStorage::PreviewMissingActorRemoval(const Path& scenePath, UUID actorID,
	std::vector<Path>& affectedFiles, std::string& error) {

	affectedFiles.clear();
	return UpdateMissingActor(scenePath, actorID, error, &affectedFiles);
}

bool Engine::SceneAssetStorage::UpdateMissingActor(const Path& scenePath, UUID actorID,
	std::string& error, std::vector<Path>* preview) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) { error = "シーンの保存・削除・修復処理中です"; return false; }
	try {
		const AssetID id = ReadSceneID(scenePath);
		RequireClosed(id);
		const Path actorRoot = ResolveActorRoot(scenePath, id);
		if (!actorID || std::filesystem::exists(actorRoot / (ToString(actorID) + ".actor.json"))) throw std::runtime_error("欠損したActorだけを削除確定できます");
		auto scene = JsonAdapter::Load(scenePath, false);
		auto& ids = scene.at("ExternalActors");
		const auto found = std::find(ids.begin(), ids.end(), ToString(actorID));
		if (found == ids.end()) throw std::runtime_error("シーンにないActor IDです");
		// 型を確定できない参照は書き換えず、参照元の手動修正を求める
		const std::string token = ToString(actorID);
		ids.erase(found);
		if (!ClearActorReferences(scene, id, id, token)) throw std::runtime_error("シーン本体に型を確定できないActor参照が残っています、先に参照元を修正してください");
		std::vector<SceneStorageChange> changes;
		for (const auto& actor : ids) {
			const Path path = actorRoot / (actor.get<std::string>() + ".actor.json");
			auto data = JsonAdapter::Load(path, false);
			if (!data.is_object()) throw std::runtime_error("別のActorも欠損または不正です");
			bool reparent = false;
			auto& components = data.at("Components");
			if (components.contains("Hierarchy") && components["Hierarchy"].value("parentLocalFileID", std::string{}) == token) {
				components["Hierarchy"]["parentLocalFileID"] = "";
				reparent = true;
			}
			const auto beforeReferences = data;
			if (!ClearActorReferences(data, id, id, token)) throw std::runtime_error("型を確定できないActor参照が残っています: " + Algorithm::PathToUTF8(path));
			if (reparent || data != beforeReferences) changes.push_back({ path, data });
		}
		// 他シーンとPrefabの明示的なScene参照も同じ操作で更新する
		for (const Path& root : { RuntimePaths::GetGameAssetsRoot(), RuntimePaths::GetEngineAssetsRoot() }) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
				const Path path = entry.path();
				const std::string name = path.filename().string();
				if (!entry.is_regular_file() || PathKey(path) == PathKey(scenePath) || IsInside(path, actorRoot) ||
					(!name.ends_with(".scene.json") && !name.ends_with(".prefab.json") && !name.ends_with(".actor.json"))) continue;
				auto data = JsonAdapter::Load(path, false);
				if (!data.is_object()) continue;
				const auto beforeReferences = data;
				ClearActorReferences(data, {}, id, token);
				if (data == beforeReferences) continue;
				if (name.ends_with(".scene.json")) RequireClosed(ReadSceneID(path));
				if (name.ends_with(".actor.json")) RequireClosed(FromString32Hex(path.parent_path().filename().string()));
				changes.push_back({ path, data });
			}
		}
		changes.push_back({ scenePath, scene });
		if (preview) {
			for (const auto& change : changes) preview->push_back(change.path);
			return true;
		}
		if (!Commit(changes, "欠損Actorの削除確定", error)) return false;
		loadedRevisions_.erase(PathKey(scenePath));
		return true;
	} catch (const std::exception& exception) { error = exception.what(); return false; }
}

std::vector<std::filesystem::path> Engine::SceneAssetStorage::GetRecoveries(bool unfinishedOnly) {

	std::lock_guard lock(storageMutex_);
	return SceneStorageJournal::GetRecoveries(unfinishedOnly);
}

bool Engine::SceneAssetStorage::RecoverInternal(const Path& directory, std::string& error, bool rollingBack) {

	std::unique_lock lock(storageMutex_, std::try_to_lock);
	if (!lock.owns_lock()) { error = "シーンの保存・削除・修復処理中です"; return false; }
	return SceneStorageJournal::Recover(directory, error, [&](const Path& target) {
		const auto loaded = loadedAssets_.find(PathKey(target));
		if (!rollingBack && loaded != loadedAssets_.end()) RequireClosed(loaded->second);
		for (AssetID id : protectedScenes_) {
			if (!rollingBack && (Algorithm::PathToUTF8(target).find(ToString(id)) != std::string::npos ||
				(AssetTypeResolver::GuessByPath(target) == AssetType::Scene && std::filesystem::exists(Path(target.wstring() + L".meta")) && ReadSceneID(target) == id))) {
				throw std::runtime_error("読み込み中シーンの復旧はできません");
			}
		}
	});
}

bool Engine::SceneAssetStorage::Commit(const std::vector<SceneStorageChange>& changes, const std::string& label, std::string& error) {

	return SceneStorageJournal::Commit(changes, label, error, [this](const Path& directory, std::string& rollbackError) {
		const bool recovered = RecoverInternal(directory, rollbackError, true);
		return recovered;
	});
}

void Engine::SceneAssetStorage::RequireClosed(AssetID id) {

	if (protectedScenes_.contains(id)) throw std::runtime_error("読み込み中のシーンです、先に閉じるかアンロードしてください");
}

bool Engine::SceneAssetStorage::Recover(const Path& directory, std::string& error) {

	return RecoverInternal(directory, error, false);
}
