#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace NEMTests {

	// 公開前の記録と中断時の所有と衝突保護を確認
	bool CheckStorageFilePublication(const std::filesystem::path& root);
}
