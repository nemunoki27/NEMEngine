#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <memory>
// json
#include <json.hpp>

namespace Engine {

	class SceneAssetStorage;

	// シーン本体と外部Actorの複製先
	struct SceneAssetCopy {

		std::filesystem::path sourcePath;
		std::filesystem::path targetPath;
	};

	// 保存要求時点のWorldから確定したシーン保存データ
	struct SceneSaveSnapshot {

		std::filesystem::path scenePath;
		AssetID sceneAsset{};
		nlohmann::json root{};
		bool useExternalActors = false;
		// 保存完了まで編集セッションの状態を保持する
		std::shared_ptr<SceneAssetStorage> storage;
	};

} // Engine
