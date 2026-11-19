#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// 表面のShaderを生成する
	std::string BuildSurfaceSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.outputNode);
		const bool is3D = IsShaderGraph3DTarget(graph.target);
		const ShaderGraphNodeKind expectedOutput = is3D ? ShaderGraphNodeKind::SurfaceOutput : ShaderGraphNodeKind::UnlitOutput;
		if (!output || output->kind != expectedOutput) {
			context.AddDiagnostic(
				graph.outputNode, is3D ? "PBR Surface出力ノードが見つかりません" : "Unlit Surface出力ノードが見つかりません");
			return {};
		}

		// 出力ピンから表面の値を生成する
		const ShaderGraphExpression baseColor =
			context.EmitInput(*output, 0, ShaderGraphValueType::Float4, "float4(1.0f, 1.0f, 1.0f, 1.0f)");
		const ShaderGraphExpression normal = is3D ?
			context.EmitInput(*output, 1, ShaderGraphValueType::Float3, "graphInput.worldNormal") :
			ShaderGraphExpression{ShaderGraphValueType::Float3, "graphInput.worldNormal"};
		const ShaderGraphExpression metallic = is3D ?
			context.EmitInput(*output, 2, ShaderGraphValueType::Float, "0.0f") :
			ShaderGraphExpression{ShaderGraphValueType::Float, "0.0f"};
		const ShaderGraphExpression roughness = is3D ?
			context.EmitInput(*output, 3, ShaderGraphValueType::Float, "0.5f") :
			ShaderGraphExpression{ShaderGraphValueType::Float, "0.5f"};
		const ShaderGraphExpression ao = is3D ?
			context.EmitInput(*output, 4, ShaderGraphValueType::Float, "1.0f") :
			ShaderGraphExpression{ShaderGraphValueType::Float, "1.0f"};
		const ShaderGraphExpression emissive = is3D ?
			context.EmitInput(*output, 5, ShaderGraphValueType::Float3, "0.0f.xxx") :
			ShaderGraphExpression{ShaderGraphValueType::Float3, "0.0f.xxx"};
		const ShaderGraphExpression opacity = context.EmitInput(*output, is3D ? 6u : 1u, ShaderGraphValueType::Float, "1.0f");
		const ShaderGraphExpression alphaClip = context.EmitInput(*output, is3D ? 7u : 2u, ShaderGraphValueType::Float, "0.0f");
		// 透過はOpacityから推定せず専用入力で指定
		const ShaderGraphExpression transmission = is3D ? context.EmitInput(*output, 8u, ShaderGraphValueType::Float, "0.0f") :
			ShaderGraphExpression{ShaderGraphValueType::Float, "0.0f"};
		const ShaderGraphExpression transmissionColor = is3D ? context.EmitInput(*output, 9u, ShaderGraphValueType::Float3, "1.0f.xxx") :
			ShaderGraphExpression{ShaderGraphValueType::Float3, "1.0f.xxx"};

		const auto refraction = is3D ? context.EmitInput(*output, 10u, ShaderGraphValueType::Boolean, "0u") :
			ShaderGraphExpression{ShaderGraphValueType::Boolean, "0u"};

		std::string source = BuildCommonSource(graph, context);
		source += "#ifndef NEM_GENERATED_SHADER_GRAPH_SURFACE\n#define NEM_GENERATED_SHADER_GRAPH_SURFACE\n\n";
		source += "ShaderGraphSurface EvaluateShaderGraphSurface(\n"
				  "\tShaderGraphSurfaceInput graphInput,\n"
				  "\tShaderGraphParameters graphParameters) {\n\n"
				  "\tShaderGraphSurface result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.baseColor = " + baseColor.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.metallic = saturate(" + metallic.code + ");\n";
		source += "\tresult.roughness = max(saturate(" + roughness.code + "), 0.04f);\n";
		source += "\tresult.ambientOcclusion = saturate(" + ao.code + ");\n";
		source += "\tresult.emissive = " + emissive.code + ";\n";
		source += "\tresult.opacity = saturate(" + opacity.code + ");\n";
		source += "\tresult.alphaClip = saturate(" + alphaClip.code + ");\n";
		source += "\tresult.transmission = saturate(" + transmission.code + ");\n";
		source += "\tresult.transmissionColor = saturate(" + transmissionColor.code + ");\n";
		source += "\tresult.refraction = (" + refraction.code + " != 0u);\n";
		source += "\treturn result;\n"
				  "}\n\n"
				  "#endif // NEM_GENERATED_SHADER_GRAPH_SURFACE\n";
		return source;
	}

} // Engine::ShaderGraphStageSource
