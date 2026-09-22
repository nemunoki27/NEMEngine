#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

#include <utility>
#include <vector>

namespace Engine::ProjectAssetDocumentPatch {

	// JSONアセット内部の表示名をファイル名へ合わせて更新する
	void PatchJsonAssetName(const std::filesystem::path& path, AssetType type, bool resetGuid);

	// 複製アセットの表示名を更新する、GUIDは新しい.metaで発行する
	void PatchDuplicatedJsonAsset(const std::filesystem::path& path, AssetType type);

	// リネームしたアセットの名前のみを更新する
	void PatchRenamedJsonAsset(const std::filesystem::path& path, AssetType type);

	// 複製ディレクトリ内の全JSONアセットを一括修正する
	void PatchDuplicatedDirectoryAssets(const std::filesystem::path& duplicatedDirectory);

	// コピー対象から除外すべきファイル(.meta等)かを判定する
	bool ShouldSkipCopyFile(const std::filesystem::path& path);

	// アセットに随行するサイドカーファイルのパス一覧を作る
	std::vector<std::filesystem::path> BuildAssetSidecarPaths(
		const ProjectAssetEntry& asset, const std::filesystem::path& assetPath);
}
