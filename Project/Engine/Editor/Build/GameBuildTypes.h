#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

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
	// 継続確認が必要な参照欠損
	struct GameBuildWarning {

		AssetID assetID{};
		AssetID referenceID{};
		std::string assetPath;
		std::string detail;

		bool operator==(const GameBuildWarning&) const = default;
	};
	// 製品ビルド設定
	struct GameBuildSettings {

		AssetID startupScene{};
		std::string executableName;
		std::filesystem::path outputRoot;
		uint32_t gameWidth = 1920;
		uint32_t gameHeight = 1080;
		bool startupFullscreen = false;
		// この一覧だけを確認済みとして扱う
		std::vector<GameBuildWarning> confirmedWarnings;
	};
	// 製品ビルドの進行状態
	enum class GameBuildState {

		Idle,
		AwaitingConfirmation,
		Building,
		Completed,
		Failed,
	};

}
