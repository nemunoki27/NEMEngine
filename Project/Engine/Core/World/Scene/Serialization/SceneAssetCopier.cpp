#include "SceneAssetCopier.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>

// c++
#include <utility>
#include <unordered_set>
#include <vector>

using namespace Engine::SceneDocument;

namespace {

	void RemapCopiedSceneReferences(nlohmann::json& value,
		Engine::AssetID sourceAsset, Engine::AssetID targetAsset) {

		if (value.is_object()) {

			if (value.contains("kind") && value["kind"] == "Scene" &&
				value.contains("localFileId") && value.contains("sourceAsset") &&
				value["sourceAsset"].is_string() &&
				Engine::TryParseAssetGUID32Hex(value["sourceAsset"].get<std::string>()) == sourceAsset) {

				value["sourceAsset"] = Engine::ToString(targetAsset);
			}
			for (auto& child : value.items()) {

				RemapCopiedSceneReferences(child.value(), sourceAsset, targetAsset);
			}
		} else if (value.is_array()) {

			for (auto& child : value) {

				RemapCopiedSceneReferences(child, sourceAsset, targetAsset);
			}
		}
	}
}

bool Engine::SceneAssetCopier::CopySceneAssets(const std::vector<SceneAssetCopy>& copies, std::string& error,
	std::shared_ptr<SceneAssetStorage> storage) {

	struct CopyEntry {

		SceneSaveSnapshot snapshot;
		AssetMeta meta;
		std::filesystem::path metaPath;
		std::filesystem::path actorRoot;
		bool started = false;
		bool ownsActors = false;
	};

	if (!storage) {
		storage = std::make_shared<SceneAssetStorage>();
	}
	error.clear();
	std::vector<CopyEntry> entries;
	std::unordered_set<std::string> targetPaths;
	std::filesystem::path currentPath;
	// 今回作成したファイルだけを取り消し、元のシーンには触れない
	const auto fail = [&](const char* message) {

		error = std::string(message) + " 対象=" + Algorithm::PathToUTF8(currentPath);
		Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] {}", error);
		for (const CopyEntry& entry : entries) {

			if (!entry.started) {
				continue;
			}
			std::error_code ec;
			for (const std::filesystem::path& path : { entry.snapshot.scenePath, entry.metaPath }) {

				std::filesystem::remove(path, ec);
				if (ec) {
					Logger::Output(LogType::Engine, spdlog::level::err,
						"[SceneSystem] 複製途中のファイルを削除できません path={}", Algorithm::PathToUTF8(path));
				}
			}
			if (entry.ownsActors) {

				std::filesystem::remove_all(entry.actorRoot, ec);
				if (ec) {
					Logger::Output(LogType::Engine, spdlog::level::err,
						"[SceneSystem] 複製途中のActorを削除できません path={}", Algorithm::PathToUTF8(entry.actorRoot));
				}
			}
		}
		return false;
	};

	try {

		// 全シーンを検証してから書き込みを開始する
		for (const SceneAssetCopy& copy : copies) {

			currentPath = copy.sourcePath;
			CopyEntry entry{};
			entry.snapshot.storage = storage;
			std::filesystem::path sourceMetaPath = copy.sourcePath;
			sourceMetaPath += L".meta";
			if (!AssetDatabase::ReadMetaFile(sourceMetaPath, entry.meta) || entry.meta.type != AssetType::Scene) {
				return fail("複製元シーンのメタデータが不正です");
			}
			const AssetID sourceAsset = entry.meta.guid;
			entry.snapshot.root = JsonAdapter::Load(copy.sourcePath, false);
			if (!ValidateSceneFileRoot(entry.snapshot.root)) {
				return fail("複製元シーンの保存形式が不正です");
			}
			entry.snapshot.useExternalActors = entry.snapshot.root.contains("ExternalActors");
			if (entry.snapshot.useExternalActors &&
				!LoadExternalActors(copy.sourcePath, sourceAsset, entry.snapshot.root)) {
				return fail("複製元シーンの外部Actorを読み込めません");
			}
			if (!ValidateSerializedLocalFileIDs(entry.snapshot.root)) {
				return fail("複製元シーンのLocalFileIDが不正です");
			}
			for (const auto& entity : entry.snapshot.root["Entities"]) {

				if (!entity.contains("Components") || !entity["Components"].is_object()) {
					return fail("複製元シーンのコンポーネントが不正です");
				}
			}
			currentPath = copy.targetPath;
			entry.snapshot.scenePath = NormalizePath(copy.targetPath);
			entry.metaPath = entry.snapshot.scenePath;
			entry.metaPath += L".meta";
			if (copy.targetPath.empty() || RuntimePaths::ToAssetPath(entry.snapshot.scenePath).empty() ||
				!std::filesystem::is_directory(entry.snapshot.scenePath.parent_path()) ||
				std::filesystem::exists(entry.snapshot.scenePath) || std::filesystem::exists(entry.metaPath) ||
				std::filesystem::exists(entry.snapshot.scenePath.wstring() + L".tmp") ||
				!targetPaths.insert(Algorithm::ToLower(Algorithm::PathToUTF8(entry.snapshot.scenePath))).second) {
				return fail("シーンの複製先が存在するか使用できません");
			}
			entry.meta.guid = AssetGUID::New();
			entry.meta.assetPath = RuntimePaths::ToAssetPath(entry.snapshot.scenePath);
			entry.snapshot.sceneAsset = entry.meta.guid;
			entry.actorRoot = ResolveExternalActorsRoot(entry.snapshot.scenePath, entry.meta.guid);
			if (entry.actorRoot.empty() || std::filesystem::exists(entry.actorRoot)) {
				return fail("シーンの複製先Actorフォルダーを使用できません");
			}
			entry.snapshot.root["Header"]["name"] = MakeSceneAssetName(copy.targetPath);
			RemapCopiedSceneReferences(entry.snapshot.root, sourceAsset, entry.meta.guid);
			entries.emplace_back(std::move(entry));
		}
		for (CopyEntry& entry : entries) {

			currentPath = entry.snapshot.scenePath;
			if (std::filesystem::exists(currentPath) || std::filesystem::exists(entry.metaPath)) {
				return fail("シーンの複製先が既に存在します");
			}
			entry.started = true;
			if (entry.snapshot.useExternalActors) {

				std::filesystem::create_directories(entry.actorRoot.parent_path());
				entry.ownsActors = std::filesystem::create_directory(entry.actorRoot);
				if (!entry.ownsActors) {
					return fail("複製先のActorフォルダーを作成できません");
				}
			}
			if (!AssetDatabase::WriteMetaFile(entry.metaPath, entry.meta) ||
				!SceneSystem::WriteSaveSnapshot(entry.snapshot)) {
				return fail("シーンの複製データを書き込めません");
			}
		}
	} catch (const std::exception&) {

		return fail("シーンの複製中に読み書きできないデータが見つかりました");
	}
	return true;
}
