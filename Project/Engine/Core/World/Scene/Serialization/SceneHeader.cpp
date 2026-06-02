#include "SceneHeader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstring>
#include <filesystem>

//============================================================================
//	SceneHeader classMethods
//============================================================================

namespace {

	constexpr const char* kCollisionSettingsRoot = "GameAssets/Collision";
	constexpr const char* kPostProcessStackRoot = "GameAssets/PostProcess";

	bool StartsWith(const std::string& text, const char* prefix) {

		return text.rfind(prefix, 0) == 0;
	}

	std::string StripScenesPrefix(const std::string& assetPath, const char* scenesPrefix) {

		if (!StartsWith(assetPath, scenesPrefix)) {
			return {};
		}

		std::string relative = assetPath.substr(std::strlen(scenesPrefix));
		if (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
			relative.erase(relative.begin());
		}
		return relative;
	}

	std::string MakePostProcessStackFileName(const std::filesystem::path& scenePath) {

		std::filesystem::path stem = scenePath.stem();
		if (stem.extension() == ".scene") {
			stem = stem.stem();
		}

		std::string name = stem.string();
		if (name.empty()) {
			name = "Scene";
		}
		return name + ".postProcessStack.json";
	}

	std::string MakeCollisionSettingsFileName(const std::filesystem::path& scenePath) {

		std::filesystem::path stem = scenePath.stem();
		if (stem.extension() == ".scene") {
			stem = stem.stem();
		}

		std::string name = stem.string();
		if (name.empty()) {
			name = "Scene";
		}
		return name + ".collisionSettings.json";
	}

	std::filesystem::path MakeCollisionRelativeSource(const std::string& scenePath) {

		std::string assetPath = Engine::RuntimePaths::ToAssetPath(scenePath);
		if (assetPath.empty()) {
			assetPath = std::filesystem::path(scenePath).filename().generic_string();
		}

		if (std::string relative = StripScenesPrefix(assetPath, "GameAssets/Scenes"); !relative.empty()) {
			return relative;
		}
		if (std::string relative = StripScenesPrefix(assetPath, "Engine/Assets/Scenes"); !relative.empty()) {
			return relative;
		}
		return std::filesystem::path(assetPath).filename();
	}

}

std::string Engine::MakeDefaultCollisionSettingsPath(const std::string& scenePath) {

	const std::filesystem::path relativeSource = MakeCollisionRelativeSource(scenePath);
	std::filesystem::path settingsPath = kCollisionSettingsRoot;
	if (relativeSource.has_parent_path()) {
		settingsPath /= relativeSource.parent_path();
	}
	settingsPath /= MakeCollisionSettingsFileName(relativeSource);
	return settingsPath.generic_string();
}

void Engine::EnsureSceneCollisionSettings(SceneHeader& sceneHeader, const std::string& scenePath, AssetDatabase* assetDatabase) {

	if (sceneHeader.collisionSettings || !assetDatabase) {
		return;
	}
	const std::string defaultPath = MakeDefaultCollisionSettingsPath(scenePath);
	if (std::filesystem::exists(assetDatabase->ResolveAssetPath(defaultPath))) {
		sceneHeader.collisionSettings = assetDatabase->ImportOrGet(defaultPath, AssetType::CollisionSettings);
	}
}

std::string Engine::MakeDefaultPostProcessStackPath(const std::string& scenePath) {

	const std::filesystem::path relativeSource = MakeCollisionRelativeSource(scenePath);
	std::filesystem::path stackPath = kPostProcessStackRoot;
	if (relativeSource.has_parent_path()) {
		stackPath /= relativeSource.parent_path();
	}
	stackPath /= MakePostProcessStackFileName(relativeSource);
	return stackPath.generic_string();
}

void Engine::EnsureScenePostProcessStack(SceneHeader& sceneHeader, const std::string& scenePath, AssetDatabase* assetDatabase) {

	if (sceneHeader.postProcessStack || !assetDatabase) {
		return;
	}
	const std::string defaultPath = MakeDefaultPostProcessStackPath(scenePath);
	if (std::filesystem::exists(assetDatabase->ResolveAssetPath(defaultPath))) {
		sceneHeader.postProcessStack = assetDatabase->ImportOrGet(defaultPath, AssetType::PostProcessStack);
	}
}

bool Engine::FromJson(const nlohmann::json& data, SceneHeader& sceneHeader, AssetDatabase* assetDatabase) {

	// JSONがオブジェクトでない場合は失敗
	if (!data.is_object()) {
		return false;
	}

	// JSONからシーンヘッダーの情報を取得する
	{
		std::string guidStr = data.value("guid", "");
		sceneHeader.guid = guidStr.empty() ? UUID::New() : FromString16Hex(guidStr);
		sceneHeader.name = data.value("name", "UntitledScene");
		sceneHeader.collisionSettings = ParseAssetReference(data, "collisionSettings", assetDatabase, AssetType::CollisionSettings);
		sceneHeader.postProcessStack = ParseAssetReference(data, "postProcessStack", assetDatabase, AssetType::PostProcessStack);
	}

	// サブシーン
	sceneHeader.subScenes.clear();
	if (data.contains("subScenes") && data["subScenes"].is_array()) {

		size_t generatedIndex = 0;
		for (const auto& item : data["subScenes"]) {

			SubSceneSlotDesc desc{};
			if (item.is_string()) {

				desc.slotName = "SubScene" + std::to_string(generatedIndex++);
				nlohmann::json temp = { {"sceneAsset", item.get<std::string>()} };
				desc.sceneAsset = ParseAssetReference(temp, "sceneAsset", assetDatabase, AssetType::Scene);
				desc.enabled = true;
			} else if (item.is_object()) {

				desc.slotName = item.value("slotName", "SubScene" + std::to_string(generatedIndex++));
				desc.sceneAsset = ParseAssetReference(item, "sceneAsset", assetDatabase, AssetType::Scene);
				desc.enabled = item.value("enabled", true);
			} else {
				continue;
			}
			if (desc.slotName.empty()) {
				desc.slotName = "SubScene" + std::to_string(generatedIndex++);
			}
			if (!desc.sceneAsset) {
				continue;
			}
			sceneHeader.subScenes.emplace_back(std::move(desc));
		}
	}

	return true;
}

nlohmann::json Engine::ToJson(const SceneHeader& sceneHeader) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(sceneHeader.guid);
	data["name"] = sceneHeader.name;
	data["collisionSettings"] = ToAssetReferenceJson(sceneHeader.collisionSettings);
	data["postProcessStack"] = ToAssetReferenceJson(sceneHeader.postProcessStack);

	data["subScenes"] = nlohmann::json::array();
	for (const auto& subScene : sceneHeader.subScenes) {
		nlohmann::json item = nlohmann::json::object();
		item["slotName"] = subScene.slotName;
		item["sceneAsset"] = ToAssetReferenceJson(subScene.sceneAsset);
		item["enabled"] = subScene.enabled;
		data["subScenes"].push_back(item);
	}

	return data;
}
