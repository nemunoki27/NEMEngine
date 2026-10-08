#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
#include <string>

namespace Engine::SceneStorageFiles {

	using Path = std::filesystem::path;

	// 大小文字と相対要素を除いた比較キーを取得する
	std::string PathKey(const Path& path);

	// パスが指定ルート内にあるか調べる
	bool IsInside(const Path& path, const Path& root);

	// 編集可能なAssetルート内か検証する
	bool IsWritable(const Path& path);

	// 欠損と読込失敗を区別して内容のhashを取得する
	std::string FileRevision(const Path& path);
}
