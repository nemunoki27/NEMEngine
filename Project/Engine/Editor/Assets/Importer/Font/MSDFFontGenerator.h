#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <string>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	MSDFFontGenerator
	//	Fontソースから画像と文字配置を生成する
	//============================================================================
	namespace MSDFFontGenerator {

		// 生成結果
		struct Result {

			bool success = false;
			AssetID fontAssetID{};		// Fontの識別子
			std::string fontAssetPath;	// Fontの論理パス
			AssetID atlasAssetID{};		// Atlasの識別子
			std::string atlasAssetPath; // Atlasの論理パス
			std::string message;		// 失敗時の理由
		};

		// 対応するFontソースか調べる
		bool IsFontSourceExtension(const std::filesystem::path& path);

		// FontとAtlasを生成し、一組で登録する
		Result EnsureGenerated(AssetDatabase& database, const std::filesystem::path& fontSourcePath, bool forceRegenerate);
	}
}
