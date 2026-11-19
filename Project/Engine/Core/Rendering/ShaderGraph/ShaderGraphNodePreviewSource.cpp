#include "ShaderGraphNodePreviewSource.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphStageSource.h"
#include "ShaderGraphRuntimeInputSource.h"

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

std::string Engine::ShaderGraphNodePreviewSource::Build(const ShaderGraphAsset& graph,
	ShaderGraphExpressionCompiler& compiler, const ShaderGraphExpression& value, const ShaderGraphNodePreviewOptions& options) {

	std::string source = "#define NEM_SHADER_GRAPH_NODE_PREVIEW\n"
		"static const uint kNoTexture = 0xFFFFFFFFu;\nSamplerState gSampler : register(s0);\n"
		"#define gSourceColor gShaderGraphSceneColor\n#define resolution (previewSettings.zz)\n";
	source += ShaderGraphStageSource::BuildCommonSource(graph, compiler, true);
	source += compiler.BuildMaterialConstantBuffer(0);
	source += compiler.BuildMaterialParameterGetter();
	source += "cbuffer PreviewConstants : register(b1) { float4 previewSettings; };\n"
		"struct PreviewInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };\n"
		"float4 main(PreviewInput input) : SV_TARGET {\n"
		"\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput)0;\n"
		"\tfloat2 plane = input.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);\n";
	if (options.sphere) {
		source += "\tif (dot(plane, plane) > 1.0f) discard;\n"
			"\tfloat3 normal = float3(plane, sqrt(saturate(1.0f - dot(plane, plane))));\n";
	} else {
		source += "\tfloat3 normal = float3(0.0f, 0.0f, 1.0f);\n";
	}
	source += options.sphere ?
		"\tgraphInput.uv = float2(atan2(normal.x, normal.z) * 0.159154943f + 0.5f, acos(clamp(normal.y, -1.0f, 1.0f)) * 0.318309886f);\n" :
		"\tgraphInput.uv = input.uv;\n";
	source +=
		"\tgraphInput.worldNormal = normal;\n"
		"\tgraphInput.objectNormal = normal;\n"
		"\tgraphInput.worldPosition = float3(plane, normal.z);\n"
		"\tgraphInput.objectPosition = graphInput.worldPosition;\n"
		"\tgraphInput.objectTangent = normalize(float3(normal.z + 0.001f, 0.0f, -normal.x));\n"
		"\tgraphInput.tangentToWorld = float3x3(graphInput.objectTangent, cross(normal, graphInput.objectTangent), normal);\n"
		"\tgraphInput.viewDirection = normalize(float3(0.0f, 0.0f, 3.0f) - graphInput.worldPosition);\n"
		"\tgraphInput.screenPosition = float4(input.uv * previewSettings.z, 0.5f, 1.0f);\n"
		"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
		"\tgraphInput.particleColor = 1.0f.xxxx;\n"
		"\tgraphInput.particleLifetime = 1.0f;\n"
		"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n";
	source += ShaderGraphStageSource::BuildFrameInputAssignments();
	source += ShaderGraphStageSource::BuildObjectInputAssignments(
		"float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)", "float3(-1,-1,-1)", "float3(1,1,1)");
	source += "\tgraphInput.isFrontFace = true;\n";
	source += compiler.GetEvaluationStatements();
	std::string converted;
	if (value.type == ShaderGraphValueType::Texture2D) {
		converted = "SampleGraphTexture(" + value.code + ", graphInput.uv, gSampler, 1.0f.xxxx)";
	} else if (value.type == ShaderGraphValueType::Matrix4) {
		converted = "(" + value.code + ")[min((uint)(input.uv.y * 4.0f), 3u)]";
	} else if (value.type == ShaderGraphValueType::Matrix3) {
		converted = "float4((" + value.code + ")[min((uint)(input.uv.y * 3.0f), 2u)], 1.0f)";
	} else if (value.type == ShaderGraphValueType::Matrix2) {
		converted = "float4((" + value.code + ")[min((uint)(input.uv.y * 2.0f), 1u)], 0.0f, 1.0f)";
	} else if (value.type == ShaderGraphValueType::Gradient) {
		converted = "ShaderGraphSampleGradient(" + value.code + ", graphInput.uv.x)";
	} else {
		switch (ComponentCount(value.type)) {
		case 1: converted = "float4(((float)(" + value.code + ")).xxx, 1.0f)"; break;
		case 2: converted = "float4(" + value.code + ", 0.0f, 1.0f)"; break;
		case 3: converted = "float4(" + value.code + ", 1.0f)"; break;
		case 4: converted = value.code; break;
		default:
			compiler.AddDiagnostic({}, "この出力型は画像としてプレビューできません");
			return {};
		}
	}
	source += "\tfloat4 result = " + converted + ";\n";
	if (!options.rawOutput) {
		// 表示変換は式全体の計算が終わってから行う
		source += "\tif (any(isnan(result)) || any(isinf(result))) return float4(1.0f, 0.0f, 1.0f, 1.0f);\n"
			"\tresult.rgb = saturate(result.rgb);\n"
			"\tresult.a = saturate(result.a);\n";
	}
	source += "\treturn result;\n}\n";
	return source;
}
