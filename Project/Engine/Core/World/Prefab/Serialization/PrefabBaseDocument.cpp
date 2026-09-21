#include "PrefabBaseDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

using namespace Engine;

namespace {

	struct PrefabBaseCacheEntry {

		bool loaded = false;
		std::filesystem::file_time_type writeTime{};
		std::unordered_map<Engine::UUID, PrefabBaseEntity> base;
	};
	std::unordered_map<AssetID, PrefabBaseCacheEntry> prefabBaseCache;
}

std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> Engine::PrefabBaseDocument::LoadPrefabBaseEntities(
	AssetDatabase& database, AssetID prefabAsset, UUID* outRootLocalFileID) {

	std::unordered_map<UUID, PrefabBaseEntity> result;

	// プレファブファイルを読み込む
	auto fullPath = database.ResolveFullPath(prefabAsset);
	if (fullPath.empty()) {
		return result;
	}
	nlohmann::json fileJson = JsonAdapter::Load(fullPath);
	const uint32_t schemaVersion = fileJson.is_object() ? fileJson.value("SchemaVersion", 0u) : 0u;
	if (!fileJson.is_object() || schemaVersion < 1u || schemaVersion > 2u ||
		!fileJson.contains("Header") || !fileJson["Header"].is_object() ||
		!fileJson.contains("Entities") || !fileJson["Entities"].is_array() || fileJson["Entities"].empty()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] Prefabアセットの形式が不正です AssetID={} path={}",
			ToString(prefabAsset), fullPath.string());
		return result;
	}
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ルートのローカルIDを取得する
	UUID rootLocalFileID{};
	if (fileJson.contains("Header") && fileJson["Header"].is_object()) {

		const std::string rootStr = fileJson["Header"].value("rootLocalFileID", "");
		rootLocalFileID = rootStr.empty() ? UUID{} : FromString16Hex(rootStr);
	}
	if (outRootLocalFileID) {
		*outRootLocalFileID = rootLocalFileID;
	}
	if (!rootLocalFileID) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] PrefabのルートIDが不正です AssetID={}", ToString(prefabAsset));
		return {};
	}

	// 実体ごとにベース情報を構築する
	std::unordered_set<UUID> localFileIDs;
	for (const auto& entityJson : fileJson["Entities"]) {

		const std::string localStr = entityJson.value("LocalFileID", std::string{});
		const UUID localFileID = localStr.empty() ? UUID{} : FromString16Hex(localStr);
		if (!localFileID || !entityJson.contains("Components") ||
			!entityJson["Components"].is_object() || !localFileIDs.insert(localFileID).second) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] Prefab内に不正または重複したEntity IDがあります AssetID={}",
				ToString(prefabAsset));
			return {};
		}

		PrefabBaseEntity base{};
		base.localFileID = localFileID;
		base.isRoot = (localFileID == rootLocalFileID);
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {

			base.components = entityJson["Components"];
			// 親ローカルIDはHierarchyから取り出す
			if (base.components.contains("Hierarchy") && base.components["Hierarchy"].is_object()) {

				const std::string parentStr = base.components["Hierarchy"].value("parentLocalFileID", "");
				base.parentLocalFileID = parentStr.empty() ? UUID{} : FromString16Hex(parentStr);
			}
		}
		result.emplace(localFileID, std::move(base));
	}
	if (!result.contains(rootLocalFileID)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] Prefab内にルートEntityがありません AssetID={}", ToString(prefabAsset));
		return {};
	}
	return result;
}

const std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity>&
Engine::PrefabBaseDocument::LoadPrefabBaseEntitiesCached(AssetDatabase& database, AssetID prefabAsset) {

	PrefabBaseCacheEntry& entry = prefabBaseCache[prefabAsset];

	// ファイルの更新時刻を見て、変化が無ければ読み直さずキャッシュを返す
	const auto fullPath = database.ResolveFullPath(prefabAsset);
	std::error_code ec;
	const std::filesystem::file_time_type currentTime =
		fullPath.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(fullPath, ec);

	if (entry.loaded && !ec && currentTime == entry.writeTime) {
		return entry.base;
	}

	// 初回または更新があった場合だけファイルから読み直す
	entry.base = LoadPrefabBaseEntities(database, prefabAsset);
	entry.writeTime = currentTime;
	entry.loaded = true;
	return entry.base;
}

void Engine::PrefabBaseDocument::InvalidatePrefabBaseCache(AssetID prefabAsset) {

	if (prefabAsset) {
		prefabBaseCache.erase(prefabAsset);
	}
}
