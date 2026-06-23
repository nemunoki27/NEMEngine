#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <vector>

namespace Engine::ProjectTagSettings {

	// ProjectSettings/TagSettings.jsonのタグ一覧を取得する、未作成なら既定リストを使う
	const std::vector<std::string>& GetTags();

	// ファイルから読み直す
	void Reload();

} // Engine::ProjectTagSettings
