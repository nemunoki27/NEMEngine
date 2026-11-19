#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace {

	// 行列型の幅を取得する
	uint32_t MatrixWidth(ShaderGraphValueType type) {

		return type == ShaderGraphValueType::Matrix2 ? 2u : (type == ShaderGraphValueType::Matrix3 ? 3u : 4u);
	}

	// 座標空間からワールド空間へ変換する式を取得する
	std::string ToWorld(ShaderGraphCoordinateSpace space) {

		switch (space) {
		case ShaderGraphCoordinateSpace::Object: return "graphInput.objectToWorld";
		case ShaderGraphCoordinateSpace::View: return "graphInput.viewToWorld";
		case ShaderGraphCoordinateSpace::Clip: return "graphInput.clipToWorld";
		case ShaderGraphCoordinateSpace::Tangent:
			return "float4x4(float4(graphInput.tangentToWorld[0], 0), float4(graphInput.tangentToWorld[1], 0), "
				"float4(graphInput.tangentToWorld[2], 0), float4(0, 0, 0, 1))";
		default: return MakeLiteral({}, ShaderGraphValueType::Matrix4);
		}
	}

	// ワールド空間から座標空間へ変換する式を取得する
	std::string FromWorld(ShaderGraphCoordinateSpace space) {

		switch (space) {
		case ShaderGraphCoordinateSpace::Object: return "graphInput.worldToObject";
		case ShaderGraphCoordinateSpace::View: return "graphInput.worldToView";
		case ShaderGraphCoordinateSpace::Clip: return "graphInput.worldToClip";
		case ShaderGraphCoordinateSpace::Tangent:
			return "transpose(" + ToWorld(space) + ")";
		default: return MakeLiteral({}, ShaderGraphValueType::Matrix4);
		}
	}
}

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericMatrixNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	switch (node.kind) {
	case Kind::Matrix2:
	case Kind::Matrix3:
	case Kind::Matrix4:
	case Kind::MatrixConstruction: {
		const Type type = node.kind == Kind::Matrix2 ? Type::Matrix2 : (node.kind == Kind::Matrix3 ? Type::Matrix3 :
			(node.kind == Kind::MatrixConstruction ? (outputSlot == 1 ? Type::Matrix3 : (outputSlot == 2 ? Type::Matrix2 : Type::Matrix4)) : Type::Matrix4));
		const uint32_t width = MatrixWidth(type);
		std::string code = HLSLType(type) + "(";
		for (uint32_t row = 0; row < width; ++row) {
			if (row != 0) { code += ", "; }
			const auto value = MakeLiteral(MaterialParameterValue{ node.settings.matrixRows[row] }, Type::Float4);
			const auto expression = node.kind == Kind::MatrixConstruction ? EmitInputCode(node, row, Type::Float4) : value;
			code += "(" + expression + ")." + (width == 2 ? "xy" : (width == 3 ? "xyz" : "xyzw"));
		}
		code += ")";
		if (node.settings.matrixAxis == ShaderGraphMatrixAxis::Column) { code = "transpose(" + code + ")"; }
		return { type, code };
	}
	case Kind::MatrixDeterminant:
	case Kind::MatrixTranspose:
	case Kind::MatrixSplit: {
		auto value = EmitDynamicInput(node, 0, {});
		const auto& input = incoming_.find(EndpointKey{ node.id.value, 0 });
		if (input == incoming_.end()) { value.type = Type::Matrix4; }
		if (value.type != Type::Matrix2 && value.type != Type::Matrix3 && value.type != Type::Matrix4) {
			AddDiagnostic(node.id, "行列ノードには行列を接続してください");
			return {};
		}
		if (node.kind == Kind::MatrixDeterminant) { return { Type::Float, "determinant(" + value.code + ")" }; }
		if (node.kind == Kind::MatrixTranspose) { return { value.type, "transpose(" + value.code + ")" }; }
		const uint32_t width = MatrixWidth(value.type);
		if (width <= outputSlot) { return { Type::Float4, "float4(0, 0, 0, 0)" }; }
		const auto matrix = node.settings.matrixAxis == ShaderGraphMatrixAxis::Column ? "transpose(" + value.code + ")" : value.code;
		const auto row = "(" + matrix + ")[" + std::to_string(outputSlot) + "]";
		return { Type::Float4, width == 4 ? row : (width == 3 ? "float4(" + row + ", 0)" : "float4(" + row + ", 0, 0)") };
	}
	case Kind::TransformationMatrix: {
		static constexpr std::array fields{
			"objectToWorld", "worldToObject", "worldToView", "viewToWorld",
			"worldToClip", "clipToWorld", "viewToClip", "clipToView",
		};
		return { Type::Matrix4, "graphInput." + std::string(fields[static_cast<size_t>(node.settings.transformationMatrix)]) };
	}
	case Kind::Transform: {
		const auto value = EmitInputCode(node, 0, Type::Float3);
		if (node.settings.spaceFrom == node.settings.spaceTo) { return { Type::Float3, value }; }
		const auto matrix = "mul(" + ToWorld(node.settings.spaceFrom) + ", " + FromWorld(node.settings.spaceTo) + ")";
		return { Type::Float3, "ShaderGraphTransform(" + value + ", " + matrix + ", " +
			std::to_string(static_cast<uint32_t>(node.settings.transformType)) + "u, " +
			(node.settings.normalize ? "true" : "false") + ")" };
	}
	default: return {};
	}
}
