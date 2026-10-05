#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

// c++
#include <array>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// 座標変換と成分操作の式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitCoordinatesNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::TilingAndOffset: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		const ShaderGraphExpression tiling = EmitInput(node, 1, ShaderGraphValueType::Float2, "float2(1.0f, 1.0f)");
		const ShaderGraphExpression offset = EmitInput(node, 2, ShaderGraphValueType::Float2, "float2(0.0f, 0.0f)");
		return ShaderGraphExpression{
			ShaderGraphValueType::Float2,
			"((" + uv.code + " * " + tiling.code + ") + " + offset.code + ")",
		};
	}
	case ShaderGraphNodeKind::PolarCoordinates: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		const ShaderGraphExpression center = EmitInput(node, 1, ShaderGraphValueType::Float2, "float2(0.5f, 0.5f)");
		const ShaderGraphExpression radialScale = EmitInput(node, 2, ShaderGraphValueType::Float, "1.0f");
		const ShaderGraphExpression lengthScale = EmitInput(node, 3, ShaderGraphValueType::Float, "1.0f");
		const std::string delta = "((" + uv.code + ") - (" + center.code + "))";
		return ShaderGraphExpression{
			ShaderGraphValueType::Float2,
			"float2(length(" + delta + ") * 2.0f * " + radialScale.code + ", atan2((" + delta + ").x, (" + delta +
				").y) * 0.159154943f * " + lengthScale.code + ")",
		};
	}
	case ShaderGraphNodeKind::Rotate: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		const ShaderGraphExpression center = EmitInput(node, 1, ShaderGraphValueType::Float2, "float2(0.5f, 0.5f)");
		const ShaderGraphExpression rotation = EmitInput(node, 2, ShaderGraphValueType::Float, "0.0f");
		const std::string delta = "((" + uv.code + ") - (" + center.code + "))";
		return {ShaderGraphValueType::Float2, "(mul(" + delta + ", float2x2(cos(" + rotation.code + "), -sin(" + rotation.code +
												  "), sin(" + rotation.code + "), cos(" + rotation.code + "))) + " +
												  center.code + ")"};
	}
	case ShaderGraphNodeKind::Fresnel: {
		const ShaderGraphExpression normal = EmitInput(node, 0, ShaderGraphValueType::Float3, "graphInput.worldNormal");
		const ShaderGraphExpression view = EmitInput(node, 1, ShaderGraphValueType::Float3, "graphInput.viewDirection");
		const ShaderGraphExpression power = EmitInput(node, 2, ShaderGraphValueType::Float, "5.0f");
		return {ShaderGraphValueType::Float,
			"pow(1.0f - saturate(dot(normalize(" + normal.code + "), normalize(" + view.code + "))), " + power.code + ")"};
	}
	case ShaderGraphNodeKind::Dither: {
		const ShaderGraphExpression input = EmitDynamicInput(node, 0, "0.0f");
		const ShaderGraphExpression position = EmitInput(node, 1, ShaderGraphValueType::Float4, "graphInput.screenPosition");
		return {input.type, "(" + input.code + " - ShaderGraphDitherThreshold((" + position.code + ").xy))"};
	}
	case ShaderGraphNodeKind::SimpleNoise: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		const ShaderGraphExpression scale = EmitInput(node, 1, ShaderGraphValueType::Float, "10.0f");
		return {ShaderGraphValueType::Float, "ShaderGraphSimpleNoise(" + uv.code + " * " + scale.code + ")"};
	}
	case ShaderGraphNodeKind::Voronoi: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		const ShaderGraphExpression angle = EmitInput(node, 1, ShaderGraphValueType::Float, "0.0f");
		const ShaderGraphExpression density = EmitInput(node, 2, ShaderGraphValueType::Float, "5.0f");
		const std::string value = "ShaderGraphVoronoi(" + uv.code + " * " + density.code + ", " + angle.code + ")";
		return outputSlot == 0 ? ShaderGraphExpression{ShaderGraphValueType::Float, "(" + value + ").x"}
							   : ShaderGraphExpression{ShaderGraphValueType::Float, "(" + value + ").y"};
	}
	case ShaderGraphNodeKind::Split: {
		const ShaderGraphExpression input = EmitInput(node, 0, ShaderGraphValueType::Float4, "float4(0.0f, 0.0f, 0.0f, 0.0f)");
		static constexpr std::array components{
			'x',
			'y',
			'z',
			'w',
		};
		if (components.size() <= outputSlot) {
			AddDiagnostic(node.id, "Splitの出力ピンが不正です");
			return {};
		}
		return ShaderGraphExpression{
			ShaderGraphValueType::Float,
			"(" + input.code + ")." + std::string(1, components[outputSlot]),
		};
	}
	case ShaderGraphNodeKind::Combine: {
		const ShaderGraphExpression r = EmitInput(node, 0, ShaderGraphValueType::Float, "0.0f");
		const ShaderGraphExpression g = EmitInput(node, 1, ShaderGraphValueType::Float, "0.0f");
		const ShaderGraphExpression b = EmitInput(node, 2, ShaderGraphValueType::Float, "0.0f");
		const ShaderGraphExpression a = EmitInput(node, 3, ShaderGraphValueType::Float, "1.0f");
		if (outputSlot == 0) {
			return ShaderGraphExpression{
				ShaderGraphValueType::Float4,
				"float4(" + r.code + ", " + g.code + ", " + b.code + ", " + a.code + ")",
			};
		}
		if (outputSlot == 1) {
			return ShaderGraphExpression{
				ShaderGraphValueType::Float3,
				"float3(" + r.code + ", " + g.code + ", " + b.code + ")",
			};
		}
		if (outputSlot == 2) {
			return ShaderGraphExpression{
				ShaderGraphValueType::Float2,
				"float2(" + r.code + ", " + g.code + ")",
			};
		}
		AddDiagnostic(node.id, "Combineの出力ピンが不正です");
		return {};
	}
	default:
		return {};
	}
}
