#pragma once

//============================================================================
//	include
//============================================================================
#include <json.hpp>

namespace Engine {

	// 保存Fieldの補助情報とManagedの値マップを変換する
	class ScriptFieldStorage {
	public:
		static nlohmann::json ExtractValues(const nlohmann::json& serializedFields);
		static void MergeValues(nlohmann::json& serializedFields, const nlohmann::json& values);
	};
}
