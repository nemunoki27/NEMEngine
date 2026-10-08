#include "ShaderGraphImportUtility.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>
#include <variant>

using namespace Engine;
using namespace Engine::ShaderGraphImportUtility;
using Type = ShaderGraphValueType;

//============================================================================
//	ShaderGraphImportUtility functions
//============================================================================

// 変換不能な設定は候補グラフごと破棄する
void Engine::ShaderGraphImportUtility::Require(bool condition, const std::string& message) {

	if (!condition) {
		throw std::runtime_error(message);
	}
}

// 既定値と保存値の型を合わせる
MaterialParameterValue Engine::ShaderGraphImportUtility::ConvertValue(const MaterialParameterValue& value, Type type) {

	bool valid = false;
	// 保存値とGraph型の組み合わせを検証
	switch (type) {
	case Type::Float:
		valid = std::holds_alternative<float>(value.value);
		break;
	case Type::Float2:
		valid = std::holds_alternative<Vector2>(value.value);
		break;
	case Type::Float3:
		valid = std::holds_alternative<Vector3>(value.value);
		break;
	case Type::Float4:
		valid = std::holds_alternative<Vector4>(value.value);
		break;
	case Type::Color:
		valid = std::holds_alternative<Color4>(value.value);
		break;
	case Type::Texture2D:
		valid = std::holds_alternative<AssetID>(value.value);
		break;
	case Type::Boolean:
		valid = std::holds_alternative<bool>(value.value);
		break;
	case Type::Integer:
		valid = std::holds_alternative<int32_t>(value.value);
		break;
	default:
		break;
	}
	Require(valid, "パラメータ値の型をグラフへ変換できません");
	return value;
}
