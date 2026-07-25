#include "SceneHeader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstring>
#include <filesystem>

//============================================================================
//	SceneHeader classMethods
//============================================================================
namespace {

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

		std::string name = Engine::Algorithm::PathToUTF8(stem);
		if (name.empty()) {
			name = "Scene";
		}
		return name + ".postProcessStack.json";
	}

}

std::string Engine::MakeDefaultPostProcessStackPath(const std::string& scenePath) {

	// シーンが置かれているベース(GameAssets / Engine/Assets)を判定し、そのベース直下のPostProcessへ置く
	const std::filesystem::path sourcePath = Algorithm::PathFromUTF8(scenePath);
	std::string assetPath = RuntimePaths::ToAssetPath(sourcePath);
	if (assetPath.empty()) {
		assetPath = Algorithm::ConvertString(sourcePath.filename().generic_wstring());
	}

	std::string root = kPostProcessStackRoot;
	std::filesystem::path relativeSource;
	if (std::string gameRelative = StripScenesPrefix(assetPath, "GameAssets/Scenes"); !gameRelative.empty()) {
		root = "GameAssets/PostProcess";
		relativeSource = Algorithm::PathFromUTF8(gameRelative);
	} else if (std::string engineRelative = StripScenesPrefix(assetPath, "Engine/Assets/Scenes"); !engineRelative.empty()) {
		root = "Engine/Assets/PostProcess";
		relativeSource = Algorithm::PathFromUTF8(engineRelative);
	} else {
		relativeSource = Algorithm::PathFromUTF8(assetPath).filename();
	}

	std::filesystem::path stackPath = root;
	if (relativeSource.has_parent_path()) {
		stackPath /= relativeSource.parent_path();
	}
	stackPath /= Algorithm::PathFromUTF8(MakePostProcessStackFileName(relativeSource));
	return Algorithm::ConvertString(stackPath.generic_wstring());
}

void Engine::EnsureScenePostProcessStack(SceneHeader& sceneHeader, const std::string& scenePath, AssetDatabase* assetDatabase) {

	if (!assetDatabase) {
		return;
	}
	// 既に解決できる参照を持っているなら触らない
	if (sceneHeader.postProcessStack && assetDatabase->Find(sceneHeader.postProcessStack)) {
		return;
	}
	// 未参照、または保存し直しでguidが変わってリンク切れになった場合は既定ファイルから貼り直す
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
