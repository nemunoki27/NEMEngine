#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphGenericSource.h"
#include "ShaderGraphRuntimeInputSource.h"

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	std::string BuildCommonSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context,
		bool bindlessTextures, bool declareSceneResources) {

		// 各描画経路で使う表面入力を宣言する
		std::string source = "#ifndef NEM_GENERATED_SHADER_GRAPH_COMMON\n"
							 "#define NEM_GENERATED_SHADER_GRAPH_COMMON\n\n"
							 "#include \"Builtin/Common/descriptorHeapCompatibility.hlsli\"\n"
							 "#include \"Builtin/Common/sceneViewDepth.hlsli\"\n\n"
							 "#ifdef NEM_SHADER_GRAPH_EXPLICIT_LOD\n"
							 "#define NEM_SAMPLE_GRAPH_TEXTURE(texture, sampler, uv) texture.SampleLevel(sampler, uv, 0.0f)\n"
							 "#else\n"
							 "#define NEM_SAMPLE_GRAPH_TEXTURE(texture, sampler, uv) texture.Sample(sampler, uv)\n"
							 "#endif\n\n";
		if (declareSceneResources) {
			source += "Texture2D<float4> gShaderGraphSceneColor : register(t0, space4);\n"
				  "Texture2D<float> gShaderGraphSceneDepth : register(t1, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneNormal : register(t2, space4);\n"
				  "Texture2D<float4> gShaderGraphScenePosition : register(t3, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneMaterial : register(t4, space4);\n"
				  "Texture2D<float4> gShaderGraphSceneEmissive : register(t5, space4);\n"
				  "Texture2D<uint> gShaderGraphSceneFlags : register(t6, space4);\n\n";
		}
		source += BuildTimeConstantsDeclaration();
		source += context.BuildSamplerDeclarations();
		source += BuildGenericTypes();
		source += context.BuildParameterStructure();
		source += BuildSurfaceInputDeclaration();
		source += "float3 ShaderGraphDecodeSceneNormal(float3 encoded) {\n"
			"\tfloat3 normal = encoded * 2.0f - 1.0f;\n"
			"\tfloat lengthSquared = dot(normal, normal);\n"
			"\treturn lengthSquared > 1e-8f ? normal * rsqrt(lengthSquared) : float3(0.0f, 1.0f, 0.0f);\n}\n";
		source += "float2 ShaderGraphSceneFlagsDefaultUV(float2 pixelPosition, float2 previewUV) {\n"
			"#ifdef NEM_SHADER_GRAPH_NODE_PREVIEW\n"
			"\treturn previewUV;\n"
			"#else\n"
			"\tuint width, height;\n"
			"\tgShaderGraphSceneFlags.GetDimensions(width, height);\n"
			"\treturn pixelPosition / float2(max(uint2(width, height), 1u.xx));\n"
			"#endif\n}\n"
			"uint ShaderGraphLoadSceneFlags(float2 uv) {\n"
			"\tuint width, height;\n"
			"\tgShaderGraphSceneFlags.GetDimensions(width, height);\n"
			"\tif (width == 0u || height == 0u) return 0u;\n"
			"\tuint2 dimensions = uint2(width, height);\n"
			"\tuint2 pixel = min((uint2)(saturate(uv) * float2(dimensions)), dimensions - 1u);\n"
			"\treturn gShaderGraphSceneFlags.Load(int3(pixel, 0));\n}\n";
		source += "struct ShaderGraphSurface {\n\n"
				  "\tfloat4 baseColor;\n"
				  "\tfloat3 normal;\n"
				  "\tfloat metallic;\n"
				  "\tfloat roughness;\n"
				  "\tfloat ambientOcclusion;\n"
				  "\tfloat3 emissive;\n"
				  "\tfloat opacity;\n"
				  "\tfloat alphaClip;\n"
				  "\tfloat transmission;\n"
				  "\tfloat3 transmissionColor;\n"
				  "\tbool refraction;\n"
				  "};\n\n";
		const bool particleTarget = !bindlessTextures &&
			(graph.target == ShaderGraphTarget::Particle || graph.target == ShaderGraphTarget::Trail);
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
		// Particleは個別Textureを固定の入力へ配置する
		if (particleTarget) {
			uint32_t textureRegister = 1;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) {
					continue;
				}
				source +=
					"Texture2D<float4> " +
					MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id) +
					" : register(t" + std::to_string(textureRegister++) + ", space2);\n";
			}
			source +=
				"\nfloat4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n";
			uint32_t textureIndex = 0;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) {
					continue;
				}
				source +=
					"\tif (textureIndex == " + std::to_string(textureIndex++) + "u) return " +
					"NEM_SAMPLE_GRAPH_TEXTURE(" + MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id) +
					", sampler, uv);\n";
			}
			source += "\treturn fallbackValue;\n}\n\n";
		} else {
			source +=
				"float4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n"
				"\tif (textureIndex == 0xFFFFFFFFu) {\n"
				"\t\treturn fallbackValue;\n"
				"\t}\n"
				"\tTexture2D<float4> texture = NEM_TEXTURE2D(textureIndex);\n"
				"\treturn NEM_SAMPLE_GRAPH_TEXTURE(texture, sampler, uv);\n"
				"}\n\n";
		}
		if (SupportsHexTiling(graph)) {
			source += R"hlsl(
#define NEM_SHADER_GRAPH_HEX_TILING 1
#include "Builtin/Common/hexTiling.hlsli"
float4 SampleGraphHexTexture(uint index, HexTilingData hex, SamplerState sampler, float4 fallback, bool normalMap) {
	if (index == 0xffffffffu) return fallback;
	Texture2D<float4> texture = NEM_TEXTURE2D(index);
#ifdef NEM_SHADER_GRAPH_EXPLICIT_LOD
	return SampleHexLevel(texture, sampler, hex, 0.0f, normalMap);
#else
	return SampleHexGrad(texture, sampler, hex, ddx(hex.uv), ddy(hex.uv), normalMap);
#endif
}
float4 SampleGraphHexTextureLOD(uint index, HexTilingData hex, SamplerState sampler, float lod, float4 fallback, bool normalMap) {
	if (index == 0xffffffffu) return fallback;
	Texture2D<float4> texture = NEM_TEXTURE2D(index);
	return SampleHexLevel(texture, sampler, hex, lod, normalMap);
}
)hlsl";
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
		source += BuildGenericFunctions(graph, bindlessTextures);
		source += context.BuildCustomFunctionDeclarations();
		source += "#endif // NEM_GENERATED_SHADER_GRAPH_COMMON\n\n";
		return source;
	}
} // Engine::ShaderGraphStageSource
