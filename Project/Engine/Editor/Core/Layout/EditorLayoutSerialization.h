#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorLayoutTypes.h"

#include <json.hpp>

namespace Engine::EditorLayoutSerialization {

	inline constexpr int32_t kSchemaVersion = 1;

	// レイアウトを保存形式へ変換する
	nlohmann::json MakeLayoutJson(const EditorLayoutSnapshot& layout, bool imported);
	// 保存形式からレイアウトを復元する
	bool ReadLayout(const nlohmann::json& data, EditorLayoutSnapshot& outLayout, bool& outImported);
}
