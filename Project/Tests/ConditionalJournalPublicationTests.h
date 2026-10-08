#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>

namespace NEMTests {

	// 中断した公開の所有と復旧再開を確認する
	bool CheckConditionalJournalPublication(const std::filesystem::path& root);
}
