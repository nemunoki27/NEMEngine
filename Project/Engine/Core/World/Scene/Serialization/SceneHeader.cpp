#include "SceneHeader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <filesystem>
#include <unordered_set>
#include <utility>

//============================================================================
//	SceneHeader classMethods
//============================================================================
std::string Engine::MakeSceneAssetName(
	const std::filesystem::path& scenePath) {

	std::filesystem::path stem = scenePath.stem();
	if (Algorithm::ToLower(
		Algorithm::PathToUTF8(stem.extension())) == ".scene") {
		stem = stem.stem();
	}
	return Algorithm::PathToUTF8(stem);
}

bool Engine::FromJson(const nlohmann::json& data, SceneHeader& output, AssetDatabase* assetDatabase) try {

	// JSONがオブジェクトでない場合は失敗
	if (!data.is_object()) {
		return false;
	}
	if (data.contains("subScenes") && !data["subScenes"].is_array()) {
		return false;
	}
	// 全項目を読めるまで呼出し元のHeaderを保持する
	SceneHeader sceneHeader = output;

	// JSONからシーンヘッダーの情報を取得する
	{
		sceneHeader.name = data.value("name", "UntitledScene");
	}

	// サブシーン
	sceneHeader.subScenes.clear();
	if (data.contains("subScenes") && data["subScenes"].is_array()) {

		size_t generatedIndex = 0;
		std::unordered_set<UUID> slotIDs;
		for (const auto& item : data["subScenes"]) {

			if (!item.is_object()) {
				return false;
			}
			SubSceneSlotDesc desc{};
			desc.slotID = FromString16Hex(item.value("slotID", std::string{}));
			if (!desc.slotID || !slotIDs.insert(desc.slotID).second) {
				return false;
			}
			desc.slotName = item.value("slotName", "SubScene" + std::to_string(generatedIndex++));
			desc.sceneAsset = ParseAssetReference(item, "sceneAsset", assetDatabase, AssetType::Scene);
			desc.enabled = item.value("enabled", true);
			if (desc.slotName.empty()) {
				desc.slotName = "SubScene" + std::to_string(generatedIndex++);
			}
			// 未割当のスロットも名前と有効状態を保存する
			sceneHeader.subScenes.emplace_back(std::move(desc));
		}
	}

	output = std::move(sceneHeader);
	return true;
} catch (const nlohmann::json::exception& error) {
	Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSystem] Headerの形式が不正です 詳細={}", error.what());
	return false;
}

nlohmann::json Engine::ToJson(const SceneHeader& sceneHeader) {

	nlohmann::json data = nlohmann::json::object();

	data["name"] = sceneHeader.name;
	data["subScenes"] = nlohmann::json::array();
	for (const auto& subScene : sceneHeader.subScenes) {
		nlohmann::json item = nlohmann::json::object();
		item["slotID"] = subScene.slotID ? ToString(subScene.slotID) : "";
		item["slotName"] = subScene.slotName;
		item["sceneAsset"] = ToAssetReferenceJson(subScene.sceneAsset);
		item["enabled"] = subScene.enabled;
		data["subScenes"].push_back(item);
	}

	return data;
}
