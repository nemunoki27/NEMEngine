#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>

namespace Engine {

	struct EditorToolContext;

	namespace ProjectSettingsOperations {

		// 開いているWorldのタグ参照をCommandで変更する
		void RemapTags(const EditorToolContext& context, const std::string& from, const std::string& to);
	}
}
