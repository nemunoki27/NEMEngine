#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <string>

namespace Engine::ShaderGraphImportUtility {

	// 設定を表現できない場合は取り込みを中止する
	void Require(bool condition, const std::string& message);
	// 設定値とGraphの型が一致することを確認する
	MaterialParameterValue ConvertValue(const MaterialParameterValue& value, ShaderGraphValueType type);
}
