#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	AssetTypeResolver class
	// ファイルパスからAssetTypeを一意に判定する共通処理
	//	AssetDatabaseとProjectAssetFileUtilityの両方から利用し、
	// 拡張子・複合サフィックスの判定を一箇所へ集約する
	//============================================================================
	class AssetTypeResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AssetTypeResolver() = delete;
		~AssetTypeResolver() = delete;

		// ファイルパス(拡張子)からAssetTypeを推測する
		static AssetType GuessByPath(const std::filesystem::path& assetFullPath);
		// 内部にUID等を持つJSONアセット種別か(複製時の再採番対象判定に使う)
		static bool IsJsonAssetType(AssetType type);
	};
} // Engine
