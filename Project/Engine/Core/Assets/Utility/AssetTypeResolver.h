#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
#include <string_view>

namespace Engine {

	//============================================================================
	//	AssetTypeResolver class
	// ファイルパスからAssetTypeを一意に判定する共通処理
	//	AssetDatabaseとProjectAssetFileUtilityの両方から利用する
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
		// エンジン固有の複合拡張子を取得する、該当しなければ空を返す
		static std::string_view FindCompoundSuffix(
			const std::filesystem::path& assetFullPath);
		// 内部にUID等を持つJSONアセット種別か(複製時の再採番対象判定に使う)
		static bool IsJsonAssetType(AssetType type);
	};
} // Engine
