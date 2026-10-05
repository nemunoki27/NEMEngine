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

		const ShaderGraphExpression baseColor =
			context.EmitInput(*output, 0, ShaderGraphValueType::Float4, "float4(1.0f, 1.0f, 1.0f, 1.0f)");
		const ShaderGraphExpression normal =
			is3D ? context.EmitInput(*output, 1, ShaderGraphValueType::Float3, "graphInput.worldNormal")
				 : ShaderGraphExpression{
					   ShaderGraphValueType::Float3,
					   "graphInput.worldNormal",
				   };
		const ShaderGraphExpression metallic = is3D ? context.EmitInput(*output, 2, ShaderGraphValueType::Float, "0.0f")
													: ShaderGraphExpression{ShaderGraphValueType::Float, "0.0f"};
		const ShaderGraphExpression roughness = is3D ? context.EmitInput(*output, 3, ShaderGraphValueType::Float, "0.5f")
													 : ShaderGraphExpression{ShaderGraphValueType::Float, "0.5f"};
		const ShaderGraphExpression ao = is3D ? context.EmitInput(*output, 4, ShaderGraphValueType::Float, "1.0f")
											  : ShaderGraphExpression{ShaderGraphValueType::Float, "1.0f"};
		const ShaderGraphExpression emissive = is3D ? context.EmitInput(*output, 5, ShaderGraphValueType::Float3, "0.0f.xxx")
													: ShaderGraphExpression{
														  ShaderGraphValueType::Float3,
														  "0.0f.xxx",
													  };
		const ShaderGraphExpression opacity = context.EmitInput(*output, is3D ? 6u : 1u, ShaderGraphValueType::Float, "1.0f");
		const ShaderGraphExpression alphaClip = context.EmitInput(*output, is3D ? 7u : 2u, ShaderGraphValueType::Float, "0.0f");

		std::string source = "#ifndef NEM_GENERATED_SHADER_GRAPH_SURFACE\n"
							 "#define NEM_GENERATED_SHADER_GRAPH_SURFACE\n\n"
							 "#include \"Builtin/Common/descriptorHeapCompatibility.hlsli\"\n\n";
		source += "Texture2D<float4> gShaderGraphSceneColor : register(t0, space4);\n"
				  "Texture2D<float> gShaderGraphSceneDepth : register(t1, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneNormal : register(t2, space4);\n"
				  "Texture2D<float4> gShaderGraphScenePosition : register(t3, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneMaterial : register(t4, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneEmissive : register(t5, space4);\n"
				  "Texture2D<uint> gShaderGraphSceneFlags : register(t6, space4);\n\n"
				  "cbuffer ShaderGraphTimeConstants : register(b4) {\n\n"
				  "\tfloat shaderGraphTime;\n"
				  "\tfloat shaderGraphDeltaTime;\n"
				  "\tfloat shaderGraphSmoothDeltaTime;\n"
				  "\tfloat shaderGraphUnscaledTime;\n"
				  "};\n\n";
		source += context.BuildSamplerDeclarations();
		source += context.BuildParameterStructure();
		source += "\nstruct ShaderGraphSurfaceInput {\n\n"
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
				  "struct ShaderGraphSurface {\n\n"
				  "\tfloat4 baseColor;\n"
				  "\tfloat3 normal;\n"
				  "\tfloat metallic;\n"
				  "\tfloat roughness;\n"
				  "\tfloat ambientOcclusion;\n"
				  "\tfloat3 emissive;\n"
				  "\tfloat opacity;\n"
				  "\tfloat alphaClip;\n"
				  "};\n\n";
		const bool particleTarget = graph.target == ShaderGraphTarget::Particle || graph.target == ShaderGraphTarget::Trail;
		// 画面座標はピクセル単位で4x4のしきい値を繰り返す
		source += "float ShaderGraphDitherThreshold(float2 pixelPosition) {\n\n"
				  "\tstatic const float thresholds[16] = {\n"
				  "\t\t1.0f, 9.0f, 3.0f, 11.0f,\n"
				  "\t\t13.0f, 5.0f, 15.0f, 7.0f,\n"
				  "\t\t4.0f, 12.0f, 2.0f, 10.0f,\n"
				  "\t\t16.0f, 8.0f, 14.0f, 6.0f\n"
				  "\t};\n"
				  "\tuint2 cell = uint2(floor(frac(pixelPosition / 4.0f) * 4.0f));\n"
				  "\treturn thresholds[cell.x * 4u + cell.y] / 17.0f;\n"
				  "}\n\n";
		if (particleTarget) {
			uint32_t textureRegister = 1;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) { continue; }
				source +=
					"Texture2D<float4> " +
					MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id) +
					" : register(t" + std::to_string(textureRegister++) + ", space2);\n";
			}
			source +=
				"\nfloat4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n";
			uint32_t textureIndex = 0;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) { continue; }
				source +=
					"\tif (textureIndex == " + std::to_string(textureIndex++) + "u) return " +
					MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id) +
					".SampleLevel(sampler, uv, 0.0f);\n";
			}
			source += "\treturn fallbackValue;\n}\n\n";
		} else {
			source +=
				"float4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n"
				"\tif (textureIndex == 0xFFFFFFFFu) {\n"
				"\t\treturn fallbackValue;\n"
				"\t}\n"
				"\tTexture2D<float4> texture = NEM_TEXTURE2D(textureIndex);\n"
				"\treturn texture.SampleLevel(sampler, uv, 0.0f);\n"
				"}\n\n";
		}
		source += "float ShaderGraphHash(float2 value) {\n\n"
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
		source += "\treturn result;\n"
				  "}\n\n"
				  "#endif // NEM_GENERATED_SHADER_GRAPH_SURFACE\n";
		return source;
	}

} // Engine::ShaderGraphStageSource
