#include "ApplicationSceneSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

using namespace Engine;

namespace {

	// 存在するシーンを参照する設定だけを採用する
	AssetID ReadScene(const std::filesystem::path& configPath, AssetDatabase& database, const char* application) {

		if (!JsonAdapter::Check(configPath, false)) {
			return {};
		}
		const nlohmann::json data = JsonAdapter::Load(configPath, false);
		if (!data.is_object()) {
			return {};
		}

		const AssetID sceneAsset =
			ParseAssetReference(data, "activeScene", &database, AssetType::Scene);
		const std::filesystem::path fullPath =
			database.ResolveFullPath(sceneAsset);
		if (!sceneAsset || fullPath.empty() || !std::filesystem::exists(fullPath)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"{}: 設定が存在しないシーンを参照しています config={}",
				application, Algorithm::PathToUTF8(configPath));
			return {};
		}
		return sceneAsset;
	}
}

void ApplicationSceneSettings::LoadEditor(AssetDatabase& database, AssetID& activeScene, std::string& activeScenePath) {

	const auto loadSceneConfig = [&](const std::filesystem::path& configPath) {

		const AssetID sceneAsset = ReadScene(configPath, database, "EngineApplication");
		if (!sceneAsset) {
			return;
		}
		activeScene = sceneAsset;
		if (const AssetMeta* meta = database.Find(sceneAsset)) {
			activeScenePath = meta->assetPath;
		}
	};
	// 共有の起動シーンを基準にし、ユーザーが最後に開いていたシーンがあれば上書きする
	loadSceneConfig(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kStartupScene));
	loadSceneConfig(RuntimePaths::GetUserSettingsPath(ConfigPaths::kActiveScene));
}

void ApplicationSceneSettings::LoadGame(AssetDatabase& database, AssetID& activeScene) {

	std::filesystem::path selectedConfigPath;
	const auto loadSceneConfig = [&](const std::filesystem::path& configPath) {

		const AssetID sceneAsset = ReadScene(configPath, database, "GameApplication");
		if (!sceneAsset) {
			return;
		}
		activeScene = sceneAsset;
		selectedConfigPath = configPath;
	};
	// 製品ビルドはビルド設定を固定し、ローカル実行だけEditorの最終シーンを優先する
	loadSceneConfig(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kStartupScene));
	if (!RuntimePaths::IsProductBuild()) {
		loadSceneConfig(RuntimePaths::GetUserSettingsPath(ConfigPaths::kActiveScene));
	}
	if (activeScene) {
		Logger::Output(LogType::Engine, spdlog::level::info,
			"GameApplication: 起動シーンを決定しました GUID={} config={}",
			ToString(activeScene), Algorithm::PathToUTF8(selectedConfigPath));
	}
}

void ApplicationSceneSettings::Save(AssetID activeScene, bool product) {

	if (product) {
		return;
	}
	nlohmann::json data = nlohmann::json::object();
	data["activeScene"] = ToAssetReferenceJson(activeScene);
	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(ConfigPaths::kActiveScene), data);
}
