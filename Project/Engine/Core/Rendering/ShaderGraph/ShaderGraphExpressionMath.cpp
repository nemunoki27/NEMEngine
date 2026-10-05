#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

// c++
#include <utility>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// 数値演算の式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitMathNode(const ShaderGraphNode& node) {

	switch (node.kind) {
	case ShaderGraphNodeKind::Add:
	case ShaderGraphNodeKind::Subtract:
	case ShaderGraphNodeKind::Multiply:
	case ShaderGraphNodeKind::Divide:
		return EmitBinary(node,
			node.kind == ShaderGraphNodeKind::Add
				? "+"
				: (node.kind == ShaderGraphNodeKind::Subtract ? "-"
															  : (node.kind == ShaderGraphNodeKind::Multiply ? "*" : "/")));
	case ShaderGraphNodeKind::Power: {
		ShaderGraphExpression a = EmitDynamicInput(node, 0, "0.0f");
		ShaderGraphExpression b = EmitDynamicInput(node, 1, "1.0f");
		if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
			AddDiagnostic(node.id, "Powerノードには数値を接続してください");
			return {};
		}
		const ShaderGraphValueType resultType = ComponentCount(a.type) >= ComponentCount(b.type) ? a.type : b.type;
		a = ConvertExpression(std::move(a), resultType);
		b = ConvertExpression(std::move(b), resultType);
		return ShaderGraphExpression{
			resultType,
			"pow(" + a.code + ", " + b.code + ")",
		};
	}
	case ShaderGraphNodeKind::Minimum:
	case ShaderGraphNodeKind::Maximum:
	case ShaderGraphNodeKind::Dot:
	case ShaderGraphNodeKind::Cross:
	case ShaderGraphNodeKind::Distance:
	case ShaderGraphNodeKind::Reflect: {
		ShaderGraphExpression a = EmitDynamicInput(node, 0, "0.0f");
		ShaderGraphExpression b = EmitDynamicInput(node, 1, "0.0f");
		if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
			AddDiagnostic(node.id, "ベクトル演算ノードには数値を接続してください");
			return {};
		}
		const ShaderGraphValueType resultType = ComponentCount(a.type) >= ComponentCount(b.type) ? a.type : b.type;
		a = ConvertExpression(std::move(a), resultType);
		b = ConvertExpression(std::move(b), resultType);
		if (node.kind == ShaderGraphNodeKind::Dot || node.kind == ShaderGraphNodeKind::Distance) {
			return ShaderGraphExpression{
				ShaderGraphValueType::Float,
				std::string(node.kind == ShaderGraphNodeKind::Dot ? "dot(" : "distance(") + a.code + ", " + b.code + ")",
			};
		}
		if (node.kind == ShaderGraphNodeKind::Cross) {
			a = ConvertExpression(std::move(a), ShaderGraphValueType::Float3);
			b = ConvertExpression(std::move(b), ShaderGraphValueType::Float3);
			return {ShaderGraphValueType::Float3, "cross(" + a.code + ", " + b.code + ")"};
		}
		const char* functionName =
			node.kind == ShaderGraphNodeKind::Minimum ? "min" : (node.kind == ShaderGraphNodeKind::Maximum ? "max" : "reflect");
		return {resultType, std::string(functionName) + "(" + a.code + ", " + b.code + ")"};
	}
	case ShaderGraphNodeKind::Lerp: {
		ShaderGraphExpression a = EmitDynamicInput(node, 0, "0.0f");
		ShaderGraphExpression b = EmitDynamicInput(node, 1, "0.0f");
		const ShaderGraphValueType resultType = ComponentCount(a.type) >= ComponentCount(b.type) ? a.type : b.type;
		a = ConvertExpression(std::move(a), resultType);
		b = ConvertExpression(std::move(b), resultType);
		ShaderGraphExpression t = EmitInput(node, 2, ShaderGraphValueType::Float, "0.5f");
		return a.type != ShaderGraphValueType::Invalid &&
			b.type != ShaderGraphValueType::Invalid ?
			ShaderGraphExpression{
				resultType,
				"lerp(" + a.code + ", " + b.code +
					", " + t.code + ")",
			} :
			ShaderGraphExpression{};
	}
	case ShaderGraphNodeKind::OneMinus:
	case ShaderGraphNodeKind::Saturate:
	case ShaderGraphNodeKind::Sine:
	case ShaderGraphNodeKind::Cosine:
	case ShaderGraphNodeKind::Absolute:
	case ShaderGraphNodeKind::Floor:
	case ShaderGraphNodeKind::Fraction:
	case ShaderGraphNodeKind::SquareRoot:
	case ShaderGraphNodeKind::Negate:
	case ShaderGraphNodeKind::Normalize:
	case ShaderGraphNodeKind::Length: {
		ShaderGraphExpression input = EmitDynamicInput(node, 0, "0.0f");
		if (!IsNumeric(input.type)) {
			AddDiagnostic(node.id, "単項演算ノードには数値を接続してください");
			return {};
		}
		if (node.kind == ShaderGraphNodeKind::Length) { return {ShaderGraphValueType::Float, "length(" + input.code + ")"}; }
		const char* functionName =
			node.kind == ShaderGraphNodeKind::Saturate
				? "saturate"
				: (node.kind == ShaderGraphNodeKind::Sine
						  ? "sin"
						  : (node.kind == ShaderGraphNodeKind::Cosine
									? "cos"
									: (node.kind == ShaderGraphNodeKind::Absolute
											  ? "abs"
											  : (node.kind == ShaderGraphNodeKind::Floor
														? "floor"
														: (node.kind == ShaderGraphNodeKind::Fraction
																  ? "frac"
																  : (node.kind == ShaderGraphNodeKind::SquareRoot
																			? "sqrt"
																			: (node.kind == ShaderGraphNodeKind::Normalize
																					  ? "normalize"
																					  : nullptr)))))));
		return ShaderGraphExpression{
			input.type,
			node.kind == ShaderGraphNodeKind::OneMinus
				? "(1.0f - (" + input.code + "))"
				: (node.kind == ShaderGraphNodeKind::Negate ? "-(" + input.code + ")"
															: std::string(functionName) + "(" + input.code + ")"),
		};
	}
	case ShaderGraphNodeKind::Clamp:
	case ShaderGraphNodeKind::Smoothstep: {
		ShaderGraphExpression input = EmitDynamicInput(node, node.kind == ShaderGraphNodeKind::Clamp ? 0u : 2u, "0.0f");
		if (!IsNumeric(input.type)) {
			AddDiagnostic(node.id, "範囲演算ノードには数値を接続してください");
			return {};
		}
		const uint32_t firstSlot = node.kind == ShaderGraphNodeKind::Clamp ? 1u : 0u;
		ShaderGraphExpression minimum = ConvertExpression(EmitDynamicInput(node, firstSlot, "0.0f"), input.type);
		ShaderGraphExpression maximum = ConvertExpression(EmitDynamicInput(node, firstSlot + 1u, "1.0f"), input.type);
		if (node.kind == ShaderGraphNodeKind::Clamp) {
			return {input.type, "clamp(" + input.code + ", " + minimum.code + ", " + maximum.code + ")"};
		}
		return {input.type, "smoothstep(" + minimum.code + ", " + maximum.code + ", " + input.code + ")"};
	}
	case ShaderGraphNodeKind::Step: {
		ShaderGraphExpression edge = EmitDynamicInput(node, 0, "0.5f");
		ShaderGraphExpression input = EmitDynamicInput(node, 1, "0.0f");
		const ShaderGraphValueType resultType =
			ComponentCount(edge.type) >= ComponentCount(input.type) ? edge.type : input.type;
		edge = ConvertExpression(std::move(edge), resultType);
		input = ConvertExpression(std::move(input), resultType);
		return {resultType, "step(" + edge.code + ", " + input.code + ")"};
	}
	case ShaderGraphNodeKind::Branch: {
		ShaderGraphExpression predicate = EmitDynamicInput(node, 0, "0.0f");
		ShaderGraphExpression trueValue = EmitDynamicInput(node, 1, "0.0f");
		ShaderGraphExpression falseValue = EmitDynamicInput(node, 2, "0.0f");
		const ShaderGraphValueType resultType =
			ComponentCount(trueValue.type) >= ComponentCount(falseValue.type) ? trueValue.type : falseValue.type;
		trueValue = ConvertExpression(std::move(trueValue), resultType);
		falseValue = ConvertExpression(std::move(falseValue), resultType);
		return {resultType, "((" + predicate.code + ") != 0 ? " + trueValue.code + " : " + falseValue.code + ")"};
	}
	case ShaderGraphNodeKind::Remap: {
		ShaderGraphExpression input = EmitDynamicInput(node, 0, "0.0f");
		if (!IsNumeric(input.type)) {
			AddDiagnostic(node.id, "Remapノードには数値を接続してください");
			return {};
		}
		const ShaderGraphExpression inputRange = EmitInput(node, 1, ShaderGraphValueType::Float2, "float2(-1.0f, 1.0f)");
		const ShaderGraphExpression outputRange = EmitInput(node, 2, ShaderGraphValueType::Float2, "float2(0.0f, 1.0f)");
		const std::string normalized = "((" + input.code + " - (" + inputRange.code + ").x) / ((" + inputRange.code +
									   ").y - (" + inputRange.code + ").x))";
		return ShaderGraphExpression{
			input.type,
			"((" + outputRange.code + ").x + " + normalized + " * ((" + outputRange.code + ").y - (" + outputRange.code +
				").x))",
		};
	}
	default:
		return {};
	}
}
