#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace NEMTests {

	// フォルダー差替えの禁止と子ファイルの操作を確認
	bool CheckStorageDirectoryLease(const std::filesystem::path& root);
}
