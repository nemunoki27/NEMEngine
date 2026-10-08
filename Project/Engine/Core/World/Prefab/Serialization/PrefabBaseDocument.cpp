#include "PrefabBaseDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <exception>
#include <unordered_map>
#include <unordered_set>

using namespace Engine;

std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> Engine::PrefabBaseDocument::LoadPrefabBaseEntities(
	AssetDatabase& database, AssetID prefabAsset, UUID* outRootLocalFileID) try {

	std::unordered_map<UUID, PrefabBaseEntity> result;
	if (outRootLocalFileID) {
		*outRootLocalFileID = {};
	}

	// プレファブファイルを読み込む
	std::filesystem::path fullPath;
	nlohmann::json fileJson;
	if (!PrefabDocument::Read(database, prefabAsset, fullPath, fileJson)) {
		return result;
	}
	if (fileJson["Entities"].empty()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] Prefabアセットの形式が不正です AssetID={} path={}",
			ToString(prefabAsset), Algorithm::PathToUTF8(fullPath));
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
	if (outRootLocalFileID) {
		*outRootLocalFileID = rootLocalFileID;
	}
	return result;
} catch (const std::exception& error) {

	Logger::Output(LogType::Engine, spdlog::level::err,
		"[Prefab] 基準データを読み込めません AssetID={} 詳細={}", ToString(prefabAsset), error.what());
	return {};
}
