#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

#include <vector>

namespace Engine::ProjectAssetDocumentPatch {

	// 文書byteの表示名を更新し、保存は呼出し元へ任せる
	bool PrepareJsonAssetName(const std::filesystem::path& path, AssetType type, std::string& bytes);

	// metaと一時ファイルをコピーから除外する
	bool ShouldSkipCopyFile(const std::filesystem::path& path);

	// Assetが所有するmetaと付随ファイルを列挙する
	std::vector<std::filesystem::path> BuildAssetSidecarPaths(
		const ProjectAssetEntry& asset, const std::filesystem::path& assetPath);
}
