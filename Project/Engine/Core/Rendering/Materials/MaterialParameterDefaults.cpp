#include "MaterialParameterDefaults.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <algorithm>

//============================================================================
//	MaterialParameterDefaults functions
//============================================================================
bool Engine::MaterialParameterDefaults::IsColor(const ShaderConstantBufferVariable& variable) {

	if (variable.isColor) {
		return true;
	}
	// 手書きShaderの色名も従来どおり扱う
	constexpr char kColor[] = "color";
	auto toLower = [](char c) {
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
	};
	return std::search(variable.name.begin(), variable.name.end(), kColor, kColor + 5,
		[toLower](char lhs, char rhs) { return toLower(lhs) == rhs; }) != variable.name.end();
}

Engine::MaterialParameterValue Engine::MaterialParameterDefaults::BuildValue(const ShaderConstantBufferVariable& variable) {

	MaterialParameterValue result{};
	// 浮動小数点の成分数に合わせて値を作る
	if (variable.valueType == D3D_SVT_FLOAT) {

		uint32_t componentCount = GetVariableComponentCount(variable);
		if (componentCount <= 1) {
			result.value = 0.0f;
		} else if (componentCount == 2) {
			result.value = Vector2{};
		} else if (componentCount == 3) {
			result.value = Vector3{};
		} else if (IsColor(variable)) {
			result.value = Color4(1.0f, 1.0f, 1.0f, 1.0f);
		} else {
			result.value = Vector4{};
		}
	} else if (variable.valueType == D3D_SVT_INT) {
		result.value = int32_t(0);
	} else if (variable.valueType == D3D_SVT_UINT) {
		result.value = uint32_t(0);
	} else if (variable.valueType == D3D_SVT_BOOL) {
		result.value = false;
	} else {
		result.value = 0.0f;
	}
	return result;
}
