#include "SceneHeader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
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

	constexpr const char* kRenderFeatureRoot = "GameAssets/RenderFeatures";

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

	std::string MakeRenderFeatureProfileFileName(
		const std::filesystem::path& scenePath) {

		std::filesystem::path stem = scenePath.stem();
		if (stem.extension() == ".scene") {
			stem = stem.stem();
		}

		std::string name = Engine::Algorithm::PathToUTF8(stem);
		if (name.empty()) {
			name = "Scene";
		}
		return name + ".renderFeatureProfile.json";
	}

}

std::string Engine::MakeSceneAssetName(
	const std::filesystem::path& scenePath) {

	std::filesystem::path stem = scenePath.stem();
	if (Algorithm::ToLower(
		Algorithm::PathToUTF8(stem.extension())) == ".scene") {
		stem = stem.stem();
	}
	return Algorithm::PathToUTF8(stem);
}

std::string Engine::MakeDefaultRenderFeatureProfilePath(
	const std::string& scenePath) {

	// シーンが置かれているベースを判定し、そのベース直下のRenderFeaturesへ置く
	const std::filesystem::path sourcePath = Algorithm::PathFromUTF8(scenePath);
	std::string assetPath = RuntimePaths::ToAssetPath(sourcePath);
	if (assetPath.empty()) {
		assetPath = Algorithm::ConvertString(sourcePath.filename().generic_wstring());
	}

	std::string root = kRenderFeatureRoot;
	std::filesystem::path relativeSource;
	if (std::string gameRelative = StripScenesPrefix(assetPath, "GameAssets/Scenes"); !gameRelative.empty()) {
		root = "GameAssets/RenderFeatures";
		relativeSource = Algorithm::PathFromUTF8(gameRelative);
	} else if (std::string engineRelative = StripScenesPrefix(assetPath, "Engine/Assets/Scenes"); !engineRelative.empty()) {
		root = "Engine/Assets/RenderFeatures";
		relativeSource = Algorithm::PathFromUTF8(engineRelative);
	} else {
		relativeSource = Algorithm::PathFromUTF8(assetPath).filename();
	}

	std::filesystem::path profilePath = root;
	if (relativeSource.has_parent_path()) {
		profilePath /= relativeSource.parent_path();
	}
	profilePath /= Algorithm::PathFromUTF8(
		MakeRenderFeatureProfileFileName(relativeSource));
	return Algorithm::ConvertString(profilePath.generic_wstring());
}

void Engine::EnsureSceneRenderFeatureProfile(SceneHeader& sceneHeader,
	const std::string& scenePath, AssetDatabase* assetDatabase) {

	if (!assetDatabase) {
		return;
	}
	// 既に解決できる参照を持っているなら触らない
	if (sceneHeader.renderFeatureProfile &&
		assetDatabase->Find(sceneHeader.renderFeatureProfile)) {

		return;
	}
	// 未参照、または保存し直しでguidが変わってリンク切れになった場合は既定ファイルから貼り直す
	const std::string defaultPath =
		MakeDefaultRenderFeatureProfilePath(scenePath);
	if (std::filesystem::exists(assetDatabase->ResolveAssetPath(defaultPath))) {
		sceneHeader.renderFeatureProfile = assetDatabase->ImportOrGet(
			defaultPath, AssetType::RenderFeatureProfile);
		return;
	}
	// シーン固有Profileが無い場合も内蔵の標準構成を明示的に使用する
	if (assetDatabase->Find(BuiltinAssets::RenderFeatureProfiles::Default)) {
		sceneHeader.renderFeatureProfile =
			BuiltinAssets::RenderFeatureProfiles::Default;
	}
}

bool Engine::FromJson(const nlohmann::json& data, SceneHeader& sceneHeader, AssetDatabase* assetDatabase) {

	// JSONがオブジェクトでない場合は失敗
	if (!data.is_object()) {
		return false;
	}

	// JSONからシーンヘッダーの情報を取得する
	{
		sceneHeader.name = data.value("name", "UntitledScene");
		sceneHeader.renderFeatureProfile = ParseAssetReference(data,
			"renderFeatureProfile", assetDatabase,
			AssetType::RenderFeatureProfile);
	}

	// サブシーン
	sceneHeader.subScenes.clear();
	if (data.contains("subScenes") && data["subScenes"].is_array()) {

		size_t generatedIndex = 0;
		for (const auto& item : data["subScenes"]) {

			if (!item.is_object()) {
				return false;
			}
			SubSceneSlotDesc desc{};
			desc.slotID = FromString16Hex(item.value("slotID", std::string{}));
			if (!desc.slotID) {
				return false;
			}
			desc.slotName = item.value("slotName", "SubScene" + std::to_string(generatedIndex++));
			desc.sceneAsset = ParseAssetReference(item, "sceneAsset", assetDatabase, AssetType::Scene);
			desc.enabled = item.value("enabled", true);
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

	data["name"] = sceneHeader.name;
	data["renderFeatureProfile"] = ToAssetReferenceJson(
		sceneHeader.renderFeatureProfile);

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
