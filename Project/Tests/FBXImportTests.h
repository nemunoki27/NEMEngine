#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace NEMTests {

	// ASCIIと32bitと64bitの参照更新を検証する
	bool CheckFBXReferences();
	// 部品の配置と鏡映を元ノードの変換と比較する
	bool CheckFBXTransforms();
	// 外部画像を使う実FBXの検証用ファイルを準備する
	bool PrepareFBXTextures(const std::filesystem::path& directory);
	// Asset登録と静的部品と骨付きモデルの読込を検証する
	bool TestFBXImport();
}
