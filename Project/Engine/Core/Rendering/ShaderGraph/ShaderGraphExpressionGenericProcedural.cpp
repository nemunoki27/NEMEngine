#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericProceduralNode(const ShaderGraphNode& node) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	const auto uv = EmitInputCode(node, 0, Type::Float2);
	if (node.kind == Kind::GradientNoise) {
		return { Type::Float, "ShaderGraphGradientNoise(" + uv + " * " + EmitInputCode(node, 1, Type::Float) + ")" };
	}
	if (node.kind == Kind::Checkerboard) {
		const auto a = EmitInputCode(node, 1, Type::Float3);
		const auto b = EmitInputCode(node, 2, Type::Float3);
		const auto frequency = EmitInputCode(node, 3, Type::Float2);
		return { Type::Float3, "ShaderGraphCheckerboard(" + uv + ", " + a + ", " + b + ", " + frequency + ")" };
	}
	const bool polygon = node.kind == Kind::Polygon;
	const auto width = EmitInputCode(node, polygon ? 2u : 1u, Type::Float);
	const auto height = EmitInputCode(node, polygon ? 3u : 2u, Type::Float);
	if (node.kind == Kind::Ellipse) {
		return { Type::Float, "ShaderGraphEllipse(" + uv + ", " + width + ", " + height + ")" };
	}
	if (node.kind == Kind::Rectangle) {
		return { Type::Float, "ShaderGraphRectangle(" + uv + ", " + width + ", " + height + ")" };
	}
	if (node.kind == Kind::RoundedRectangle) {
		return { Type::Float, "ShaderGraphRoundedRectangle(" + uv + ", " + width + ", " + height +
			", " + EmitInputCode(node, 3, Type::Float) + ")" };
	}
	const auto sides = EmitInputCode(node, polygon ? 1u : 3u, Type::Float);
	const auto roundness = polygon ? "0.0f" : EmitInputCode(node, 4, Type::Float);
	return { Type::Float, "ShaderGraphRoundedPolygon(" + uv + ", " + width + ", " + height + ", " + sides + ", " + roundness + ")" };
}
