#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	GameBuild structures
	//============================================================================
	// ビルド対象のシーン
	struct GameBuildSceneEntry {

		AssetID assetID{};
		std::string assetPath;
		std::string displayName;
	};
	// 製品へ配置するファイル
	struct GameBuildFileEntry {

		std::filesystem::path source;
		std::string destination;
		uintmax_t size = 0;
		std::string sha256;
	};
	// 製品ビルド設定
	struct GameBuildSettings {

		AssetID startupScene{};
		std::string executableName;
		std::filesystem::path outputRoot;
		bool startupFullscreen = false;
	};
	// 製品ビルドの進行状態
	enum class GameBuildState {

		Idle,
		Building,
		Completed,
		Failed,
	};

}
