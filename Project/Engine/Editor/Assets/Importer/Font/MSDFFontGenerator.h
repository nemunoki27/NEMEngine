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
	//	.ttf/.otfからMSDFアトラスと.font.jsonをインプロセス生成するモジュール
	//============================================================================
	namespace MSDFFontGenerator {

		// 生成結果
		struct Result {

			bool success = false;
			AssetID fontAssetID{};        // 生成された.font.jsonの識別ID
			std::string fontAssetPath;    // 生成された.font.jsonの論理アセットパス
			AssetID atlasAssetID{};       // 生成されたアトラスの識別ID
			std::string atlasAssetPath;   // 生成されたアトラスの論理アセットパス
			std::string message;          // 失敗時の理由
		};

		// 拡張子が.ttf/.otfかどうか
		bool IsFontSourceExtension(const std::filesystem::path& path);

		// フォントソースの隣に<name>_msdf.font.jsonとアトラスを用意する、非強制かつ既存ならそれを再利用する
		Result EnsureGenerated(AssetDatabase& database, const std::filesystem::path& fontSourcePath, bool forceRegenerate);
	}
} // Engine
