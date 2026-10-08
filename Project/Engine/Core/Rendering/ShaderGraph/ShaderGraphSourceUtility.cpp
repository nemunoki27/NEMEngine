#include "ShaderGraphSourceUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <bit>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

namespace Engine::ShaderGraphSourceUtility {

	// 数値をHLSLリテラルへ変換する
	static std::string FormatFloat(float value) {

		std::ostringstream stream;
		stream << std::setprecision(9) << value;
		std::string result = stream.str();
		if (result.find_first_of(".eE") == std::string::npos) { result += ".0"; }
		result += "f";
		return result;
	}

	// 数値型の成分数を取得する
	uint32_t ComponentCount(ShaderGraphValueType type) {

		switch (type) {
		case ShaderGraphValueType::Float:
			return 1;
		case ShaderGraphValueType::Float2:
			return 2;
		case ShaderGraphValueType::Float3:
			return 3;
		case ShaderGraphValueType::Float4:
		case ShaderGraphValueType::Color:
			return 4;
		case ShaderGraphValueType::Boolean:
		case ShaderGraphValueType::Integer:
			return 1;
		default:
			return 0;
		}
	}

	// 数値型か判定する
	bool IsNumeric(ShaderGraphValueType type) {

		return ComponentCount(type) > 0;
	}

	// HLSLの型名を取得する
	std::string HLSLType(ShaderGraphValueType type) {

		switch (type) {
		case ShaderGraphValueType::Float:
			return "float";
		case ShaderGraphValueType::Float2:
			return "float2";
		case ShaderGraphValueType::Float3:
			return "float3";
		case ShaderGraphValueType::Float4:
		case ShaderGraphValueType::Color:
			return "float4";
		case ShaderGraphValueType::Texture2D:
			return "uint";
		case ShaderGraphValueType::SamplerState:
			return "SamplerState";
		case ShaderGraphValueType::Boolean:
			return "uint";
		case ShaderGraphValueType::Integer:
			return "int";
		case ShaderGraphValueType::Matrix4:
			return "float4x4";
		default:
			return "float";
		}
	}

	// 公開値の識別子を作る
	std::string MakeIdentifier(std::string_view name, Engine::UUID id) {

		std::string result = "p_";
		result.reserve(name.size() + 20);
		for (const char character : name) {
			const bool valid = ('a' <= character && character <= 'z') || ('A' <= character && character <= 'Z') ||
							   ('0' <= character && character <= '9') || character == '_';
			result.push_back(valid ? character : '_');
		}
		const std::string idText = ToString(id);
		result += "_";
		// ID全体を使い、同名の公開値を区別する
		result += idText;
		return result;
	}

	// Nodeの一時変数名を作る
	std::string MakeNodeVariable(std::string_view prefix, Engine::UUID id) {

		return std::string(prefix) + "_" + ToString(id);
	}

	// 保存値をHLSLリテラルへ変換する
	std::string MakeLiteral(const MaterialParameterValue& value, ShaderGraphValueType type) {

		auto component = [&](uint32_t index, float fallback) {
			return std::visit(
				[&](const auto& current) -> float {
					using ValueType = std::decay_t<decltype(current)>;
					if constexpr (std::is_same_v<ValueType, float>) {
						return index == 0 ? current : fallback;
					} else if constexpr (std::is_same_v<ValueType, Vector2>) {
						return index == 0 ? current.x : (index == 1 ? current.y : fallback);
					} else if constexpr (std::is_same_v<ValueType, Vector3>) {
						return index == 0 ? current.x : (index == 1 ? current.y : (index == 2 ? current.z : fallback));
					} else if constexpr (std::is_same_v<ValueType, Vector4>) {
						return index == 0 ? current.x : (index == 1 ? current.y : (index == 2 ? current.z : current.w));
					} else if constexpr (std::is_same_v<ValueType, Color4>) {
						return index == 0 ? current.r : (index == 1 ? current.g : (index == 2 ? current.b : current.a));
					} else if constexpr (std::is_same_v<ValueType, int32_t> || std::is_same_v<ValueType, uint32_t>) {

						return index == 0 ? static_cast<float>(current) : fallback;
					} else if constexpr (std::is_same_v<ValueType, bool>) {
						return index == 0 && current ? 1.0f : fallback;
					} else {
						return fallback;
					}
				},
				value.value);
		};

		if (type == ShaderGraphValueType::Texture2D) { return "kNoTexture"; }
		if (type == ShaderGraphValueType::Boolean) { return component(0, 0.0f) != 0.0f ? "1u" : "0u"; }
		if (type == ShaderGraphValueType::Integer) {

			// 整数はfloatを経由せず精度を保つ
			if (const auto* integer = std::get_if<int32_t>(&value.value)) {
				return std::to_string(*integer);
			}
			if (const auto* integer = std::get_if<uint32_t>(&value.value)) {
				return std::to_string(std::bit_cast<int32_t>(*integer));
			}
			// 実数の範囲外と非有限値はMaterial転送と同じく0へ戻す
			int32_t integer = 0;
			Math::TryConvertToInt32(component(0, 0.0f), integer);
			return std::to_string(integer);
		}
		if (type == ShaderGraphValueType::Matrix4) {
			return "float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)";
		}
		const uint32_t count = ComponentCount(type);
		if (count <= 1) { return FormatFloat(component(0, 0.0f)); }

		std::string result = HLSLType(type) + "(";
		for (uint32_t index = 0; index < count; ++index) {
			if (index > 0) { result += ", "; }
			result += FormatFloat(component(index, index == 3 ? 1.0f : 0.0f));
		}
		result += ")";
		return result;
	}

	// 接続値を入力の型へ変換する
	ShaderGraphExpression ConvertExpression(ShaderGraphExpression expression, ShaderGraphValueType target) {

		if (!IsNumeric(expression.type) || !IsNumeric(target)) {
			return expression.type == target ? expression : ShaderGraphExpression{};
		}
		if (expression.type == target ||
			(expression.type == ShaderGraphValueType::Color && target == ShaderGraphValueType::Float4) ||
			(expression.type == ShaderGraphValueType::Float4 && target == ShaderGraphValueType::Color)) {

			expression.type = target;
			return expression;
		}

		const uint32_t sourceCount = ComponentCount(expression.type);
		const uint32_t targetCount = ComponentCount(target);
		const std::string source = "(" + expression.code + ")";
		if (targetCount == 1) { return ShaderGraphExpression{target, source + ".x"}; }
		if (sourceCount == 1) {
			const std::string swizzle = targetCount == 2 ? ".xx" : (targetCount == 3 ? ".xxx" : ".xxxx");
			return ShaderGraphExpression{target, source + swizzle};
		}
		if (targetCount == 2) { return ShaderGraphExpression{target, source + ".xy"}; }
		if (targetCount == 3) {
			return sourceCount >= 3 ? ShaderGraphExpression{target, source + ".xyz"}
									: ShaderGraphExpression{target, "float3(" + expression.code + ", 0.0f)"};
		}
		if (sourceCount == 3) { return ShaderGraphExpression{target, "float4(" + expression.code + ", 1.0f)"}; }
		if (sourceCount == 2) { return ShaderGraphExpression{target, "float4(" + expression.code + ", 0.0f, 1.0f)"}; }
		return ShaderGraphExpression{};
	}
} // Engine::ShaderGraphSourceUtility
