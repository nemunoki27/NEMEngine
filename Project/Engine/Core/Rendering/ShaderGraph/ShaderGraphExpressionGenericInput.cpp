#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericInputNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	switch (node.kind) {
	case Kind::BitangentVector: return { Type::Float3, "graphInput.tangentToWorld[1]" };
	case Kind::VertexID: return { Type::Integer, "int(graphInput.vertexID)" };
	case Kind::IsFrontFace: return { Type::Boolean, "graphInput.isFrontFace" };
	case Kind::Blackbody:
		return { Type::Float3, "ShaderGraphBlackbody(" + EmitInputCode(node, 0, Type::Float) + ")" };
	case Kind::DielectricSpecular: {
		const auto material = node.settings.dielectricMaterial;
		if (material == ShaderGraphDielectricMaterial::Common) {
			return { Type::Float, "lerp(0.034f, 0.048f, " + EmitInputCode(node, 0, Type::Float) + ")" };
		}
		if (material == ShaderGraphDielectricMaterial::Custom) {
			const auto ior = EmitInputCode(node, 1, Type::Float);
			return { Type::Float, "pow((" + ior + " - 1.0f) / (" + ior + " + 1.0f), 2.0f)" };
		}
		switch (material) {
		case ShaderGraphDielectricMaterial::RustedMetal: return { Type::Float, "0.030f" };
		case ShaderGraphDielectricMaterial::Water: return { Type::Float, "0.020f" };
		case ShaderGraphDielectricMaterial::Ice: return { Type::Float, "0.018f" };
		case ShaderGraphDielectricMaterial::Glass: return { Type::Float, "0.040f" };
		default: return {};
		}
	}
	case Kind::MetalReflectance: {
		static constexpr std::array values{
			"float3(0.560, 0.570, 0.580)", "float3(0.972, 0.960, 0.915)", "float3(0.913, 0.921, 0.925)",
			"float3(1.000, 0.766, 0.336)", "float3(0.955, 0.637, 0.538)", "float3(0.550, 0.556, 0.554)",
			"float3(0.660, 0.609, 0.526)", "float3(0.542, 0.497, 0.449)", "float3(0.662, 0.655, 0.634)",
			"float3(0.672, 0.637, 0.585)",
		};
		return { Type::Float3, values[static_cast<size_t>(node.settings.metalMaterial)] };
	}
	case Kind::Camera: {
		static constexpr std::array fields{
			"cameraPosition", "cameraDirection", "cameraProjection.x", "cameraProjection.y",
			"cameraProjection.z", "cameraProjection.w", "cameraOrthographicSize.x", "cameraOrthographicSize.y",
		};
		return { outputSlot < 2 ? Type::Float3 : Type::Float, "graphInput." + std::string(fields[outputSlot]) };
	}
	case Kind::Object: {
		static constexpr std::array fields{
			"graphInput.objectToWorld[3].xyz",
			"float3(length(graphInput.objectToWorld[0].xyz), length(graphInput.objectToWorld[1].xyz), length(graphInput.objectToWorld[2].xyz))",
			"graphInput.worldBoundsMin", "graphInput.worldBoundsMax", "(graphInput.worldBoundsMax - graphInput.worldBoundsMin)",
		};
		return { Type::Float3, fields[outputSlot] };
	}
	case Kind::Screen: return { Type::Float, outputSlot == 0 ? "graphInput.screenSize.x" : "graphInput.screenSize.y" };
	case Kind::Gradient: {
		auto colors = node.settings.gradientColors;
		auto alphas = node.settings.gradientAlphas;
		std::stable_sort(colors.begin(), colors.end(), [](const Vector4& a, const Vector4& b) { return a.w < b.w; });
		std::stable_sort(alphas.begin(), alphas.end(), [](const Vector2& a, const Vector2& b) { return a.y < b.y; });
		const std::string variable = MakeNodeVariable("gradient", node.id);
		const auto colorCount = std::min(colors.size(), size_t{ 8 });
		const auto alphaCount = std::min(alphas.size(), size_t{ 8 });
		evaluationStatements_ += "\tShaderGraphGradient " + variable + " = ShaderGraphDefaultGradient();\n";
		if (colorCount != 0) {
			evaluationStatements_ += "\t" + variable + ".colorCount = " + std::to_string(colorCount) + "u;\n";
		}
		if (alphaCount != 0) {
			evaluationStatements_ += "\t" + variable + ".alphaCount = " + std::to_string(alphaCount) + "u;\n";
		}
		evaluationStatements_ += "\t" + variable + ".fixedMode = " +
			std::to_string(static_cast<uint32_t>(node.settings.gradientMode)) + "u;\n";
		for (size_t i = 0; i < colorCount; ++i) {
			evaluationStatements_ += "\t" + variable + ".colors[" + std::to_string(i) + "] = " +
				MakeLiteral(MaterialParameterValue{ colors[i] }, Type::Float4) + ";\n";
		}
		for (size_t i = 0; i < alphaCount; ++i) {
			evaluationStatements_ += "\t" + variable + ".alphas[" + std::to_string(i) + "] = " +
				MakeLiteral(MaterialParameterValue{ alphas[i] }, Type::Float2) + ";\n";
		}
		return { Type::Gradient, variable };
	}
	case Kind::SampleGradient: {
		const auto gradient = EmitInputCode(node, 0, Type::Gradient);
		const auto time = EmitInputCode(node, 1, Type::Float);
		return { Type::Float4, "ShaderGraphSampleGradient(" + gradient + ", " + time + ")" };
	}
	default: return {};
	}
}
