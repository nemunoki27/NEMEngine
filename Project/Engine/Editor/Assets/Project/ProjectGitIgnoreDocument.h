#pragma once

//============================================================================
//	include
//============================================================================
#include <string>

namespace Engine::ProjectGitIgnoreDocument {

	// 既存の除外規則を残してEditor専用の設定を更新する
	bool Update(const std::string& content, const std::string& relativePath, bool directory, bool included,
		std::string& updated, std::string& error);
}
