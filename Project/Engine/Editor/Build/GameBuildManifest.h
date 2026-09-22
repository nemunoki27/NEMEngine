#pragma once

//============================================================================
//	include
//============================================================================
#include "GameBuildTypes.h"

namespace Engine {
	class AssetDatabase;
	class SceneAssetStorage;
}

namespace Engine::GameBuildManifest {

	// 構築条件と依存一覧を保存して生成先を返す
	bool Write(const GameBuildSettings& settings, const AssetDatabase& database,
		std::filesystem::path& manifestPath, std::filesystem::path& outputDirectory,
		std::filesystem::path& scriptPath, std::string& error, SceneAssetStorage* sceneStorage);
}
