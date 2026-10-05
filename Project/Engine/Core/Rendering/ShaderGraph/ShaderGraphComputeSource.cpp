#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================

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
							 "#include \"Builtin/Common/descriptorHeapCompatibility.hlsli\"\n\n"
							 "cbuffer PostProcessFrameConstants : register(b0) {\n\n"
							 "\tfloat2 resolution;\n"
							 "\tfloat2 invResolution;\n"
							 "\tfloat time;\n"
							 "\tfloat deltaTime;\n"
							 "\tuint frameIndex;\n"
							 "\tfloat _pad0;\n"
							 "\tfloat cameraNear;\n"
							 "\tfloat cameraFar;\n"
							 "\tfloat _pad1;\n"
							 "\tfloat _pad2;\n"
							 "\tfloat3 cameraWorldPos;\n"
							 "\tfloat _pad3;\n"
							 "\tfloat4x4 cameraView;\n"
							 "\tfloat4x4 cameraViewInverse;\n"
							 "\tfloat4x4 cameraProjection;\n"
							 "\tfloat4x4 cameraProjectionInverse;\n"
							 "};\n\n";
		source += context.BuildMaterialConstantBuffer(1, "PostProcessParameters");
		source += "\n" + context.BuildParameterStructure();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\n" + context.BuildSamplerDeclarations();
		source += "\nTexture2D<float4> gSourceColor : register(t0);\n"
				  "Texture2D<float4> gShaderGraphSceneColor : register(t1);\n"
				  "Texture2D<float> gShaderGraphSceneDepth : register(t2);\n"
				  "Texture2D<float4> gShaderGraphSceneNormal : register(t3);\n"
				  "Texture2D<float4> gShaderGraphScenePosition : register(t4);\n"
				  "Texture2D<float4> gShaderGraphSceneMaterial : register(t5);\n"
				  "Texture2D<float4> gShaderGraphSceneEmissive : register(t6);\n"
				  "Texture2D<uint> gShaderGraphSceneFlags : register(t7);\n"
				  "RWTexture2D<float4> gDestColor : register(u0);\n"
				  "SamplerState gSampler : register(s0);\n\n"
				  "struct ShaderGraphSurfaceInput {\n\n"
				  "\tfloat2 uv;\n"
				  "\tfloat3 worldNormal;\n"
				  "\tfloat3 worldPosition;\n"
				  "\tfloat3 objectPosition;\n"
				  "\tfloat3 objectNormal;\n"
				  "\tfloat3 objectTangent;\n"
				  "\tfloat3 viewDirection;\n"
				  "\tfloat4 screenPosition;\n"
				  "\tfloat4 vertexColor;\n"
				  "\tfloat3x3 tangentToWorld;\n"
				  "};\n\n"
				  "float4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n"
				  "\tif (textureIndex == 0xFFFFFFFFu) return fallbackValue;\n"
				  "\tTexture2D<float4> texture = NEM_TEXTURE2D(textureIndex);\n"
				  "\treturn texture.SampleLevel(sampler, uv, 0.0f);\n"
				  "}\n\n"
				  "float ShaderGraphHash(float2 value) {\n\n"
				  "\treturn frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);\n"
				  "}\n\n"
				  "float ShaderGraphSimpleNoise(float2 uv) {\n\n"
				  "\tfloat2 cell = floor(uv);\n"
				  "\tfloat2 local = frac(uv);\n"
				  "\tfloat2 blend = local * local * (3.0f - 2.0f * local);\n"
				  "\tfloat a = ShaderGraphHash(cell);\n"
				  "\tfloat b = ShaderGraphHash(cell + float2(1.0f, 0.0f));\n"
				  "\tfloat c = ShaderGraphHash(cell + float2(0.0f, 1.0f));\n"
				  "\tfloat d = ShaderGraphHash(cell + float2(1.0f, 1.0f));\n"
				  "\treturn lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);\n"
				  "}\n\n"
				  "float2 ShaderGraphVoronoi(float2 uv, float angleOffset) {\n\n"
				  "\tfloat2 cell = floor(uv);\n"
				  "\tfloat2 local = frac(uv);\n"
				  "\tfloat minimumDistance = 8.0f;\n"
				  "\tfloat cellValue = 0.0f;\n"
				  "\t[unroll] for (int y = -1; y <= 1; ++y) {\n"
				  "\t\t[unroll] for (int x = -1; x <= 1; ++x) {\n"
				  "\t\t\tfloat2 offset = float2(x, y);\n"
				  "\t\t\tfloat random = ShaderGraphHash(cell + offset);\n"
				  "\t\t\tfloat2 featurePoint = 0.5f + 0.5f * float2(sin(random * 6.283185307f + angleOffset), cos(random * "
				  "6.283185307f + angleOffset));\n"
				  "\t\t\tfloat distanceValue = distance(local, offset + featurePoint);\n"
				  "\t\t\tif (distanceValue < minimumDistance) { minimumDistance = distanceValue; cellValue = random; }\n"
				  "\t\t}\n"
				  "\t}\n"
				  "\treturn float2(minimumDistance, cellValue);\n"
				  "}\n\n";
		source += context.BuildCustomFunctionDeclarations();
		source += "\n[numthreads(8, 8, 1)]\n"
				  "void main(uint3 dispatchThreadID : SV_DispatchThreadID) {\n\n"
				  "\tif (any(dispatchThreadID.xy >= uint2(resolution))) return;\n"
				  "\tShaderGraphSurfaceInput graphInput;\n"
				  "\tgraphInput.uv = (float2(dispatchThreadID.xy) + 0.5f) * invResolution;\n"
				  "\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.worldPosition = 0.0f.xxx;\n"
				  "\tgraphInput.objectPosition = 0.0f.xxx;\n"
				  "\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.screenPosition = float4(dispatchThreadID.xy, 0.0f, 1.0f);\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
				  "\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
				  "\tfloat shaderGraphTime = time;\n"
				  "\tfloat shaderGraphDeltaTime = deltaTime;\n"
				  "\tfloat shaderGraphSmoothDeltaTime = deltaTime;\n"
				  "\tfloat shaderGraphUnscaledTime = time;\n";
		source += context.GetEvaluationStatements();
		source += "\tgDestColor[dispatchThreadID.xy] = " + color.code +
				  ";\n"
				  "}\n";
		return source;
	}

} // Engine::ShaderGraphStageSource
