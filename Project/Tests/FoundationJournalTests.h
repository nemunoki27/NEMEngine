#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace NEMTests {

	// 保存失敗時の復旧と競合保護を検証する
	bool TestJsonJournal(const std::filesystem::path& root);
	// 新規保存の衝突と外部変更の保持を検証する
	bool TestJsonJournalCreation(const std::filesystem::path& root);
}
