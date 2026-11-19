#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphRuntimeInputSource.h"

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// 画面効果のShaderを生成する
	std::string BuildPostProcessSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.outputNode);
		if (!output || output->kind != ShaderGraphNodeKind::PostProcessOutput) {
			context.AddDiagnostic(graph.outputNode, "Post Process出力ノードが見つかりません");
			return {};
		}
		const ShaderGraphExpression color = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float4, "gSourceColor.SampleLevel(gSampler, graphInput.uv, 0.0f)");

		std::string source = "// Shader Graph generated PostProcess\n"
			"#define NEM_SHADER_GRAPH_EXPLICIT_LOD 1\n"

			"cbuffer PostProcessFrameConstants : register(b0) {\n"
			"\tfloat2 resolution;\n"
			"\tfloat2 invResolution;\n"
			"\tfloat time;\n"
			"\tfloat deltaTime;\n"
			"\tuint frameIndex;\n"
			"\tfloat _pad0;\n"
			"\tfloat cameraNear;\n"
			"\tfloat cameraFar;\n"
			"\tfloat2 _pad1;\n"
			"\tfloat3 cameraWorldPos;\n"
			"\tfloat _pad3;\n"
			"\tfloat4x4 cameraView;\n"
			"\tfloat4x4 cameraViewInverse;\n"
			"\tfloat4x4 cameraProjection;\n"
			"\tfloat4x4 cameraProjectionInverse;\n"
			"};\n"
			"Texture2D<float4> gSourceColor : register(t0);\n"
			"RWTexture2D<float4> gDestColor : register(u0);\n"
			"SamplerState gSampler : register(s0);\n";
		source += BuildCommonSource(graph, context, true);
		source += context.BuildMaterialConstantBuffer(1, "PostProcessParameters");
		source += context.BuildMaterialParameterGetter();
		source += "[numthreads(8, 8, 1)]\n"
			"void main(uint3 dispatchThreadID : SV_DispatchThreadID) {\n"
			"\tif (any(dispatchThreadID.xy >= uint2(resolution))) return;\n"
			"\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput)0;\n" +
			BuildFrameInputAssignments() +
			"\tgraphInput.uv = (float2(dispatchThreadID.xy) + 0.5f) * invResolution;\n"
			"\tgraphInput.worldPosition = gShaderGraphScenePosition.SampleLevel(gSampler, graphInput.uv, 0.0f).xyz;\n"
			"\tgraphInput.worldNormal = ShaderGraphDecodeSceneNormal(gShaderGraphSceneNormal.SampleLevel(gSampler, graphInput.uv, 0.0f).xyz);\n"
			"\tgraphInput.objectPosition = graphInput.worldPosition;\n"
			"\tgraphInput.objectNormal = graphInput.worldNormal;\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = normalize(graphInput.cameraPosition - graphInput.worldPosition);\n"
			"\tgraphInput.screenPosition = float4(float2(dispatchThreadID.xy) + 0.5f, 0.0f, 1.0f);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.isFrontFace = true;\n"
			"\tgraphInput.tangentToWorld = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);\n" +
			BuildObjectInputAssignments("float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)",
				"graphInput.worldPosition", "graphInput.worldPosition") +
			"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n";
		source += context.GetEvaluationStatements();
		source += "\tgDestColor[dispatchThreadID.xy] = " + color.code + ";\n}\n";
		return source;
	}
} // Engine::ShaderGraphStageSource
