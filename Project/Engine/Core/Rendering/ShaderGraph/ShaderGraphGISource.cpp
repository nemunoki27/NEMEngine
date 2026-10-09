#include "ShaderGraphStageSource.h"

//============================================================================
//	ShaderGraphStageSource functions
//============================================================================
std::string Engine::ShaderGraphStageSource::BuildGIMaterialSource(std::string_view surfaceIncludeFile,
	const ShaderGraphExpressionCompiler& context) {

	const std::string getter = context.BuildMaterialParameterGetter(true);
	// Surface評価を各MaterialのCallableへ接続
	return "#define NEM_REFLECTION_HELPERS_ONLY\n"
		"#define NEM_REFLECTION_CUSTOM_HIT\n"
		"#include \"Builtin/Raytracing/reflection.RT.hlsl\"\n"
		"#include \"Builtin/GlobalIllumination/giMaterialData.hlsli\"\n"
		"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n" + getter + "\n"
		"[shader(\"callable\")]\n"
		"void GIMaterial(inout GIMaterialData data) {\n\n"
		"\tShaderGraphSurfaceInput input;\n"
		"\tinput.uv = data.uv;\n"
		"\tinput.worldPosition = data.worldPosition;\n"
		"\tinput.worldNormal = data.worldNormal;\n"
		"\tinput.objectPosition = data.objectPosition;\n"
		"\tinput.objectNormal = data.objectNormal;\n"
		"\tinput.objectTangent = data.objectTangent;\n"
		"\tinput.viewDirection = data.viewDirection;\n"
		"\tinput.screenPosition = data.screenPosition;\n"
		"\tinput.vertexColor = data.vertexColor;\n"
		"\tinput.tangentToWorld = data.tangentToWorld;\n"
		"\tShaderGraphSurface surface = EvaluateShaderGraphSurface(input, GetShaderGraphParameters(data.parametersDescriptor));\n"
		"\tdata.baseColor = surface.baseColor;\n"
		"\tdata.normal = surface.normal;\n"
		"\tdata.metallic = surface.metallic;\n"
		"\tdata.roughness = surface.roughness;\n"
		"\tdata.ambientOcclusion = surface.ambientOcclusion;\n"
		"\tdata.emissive = surface.emissive;\n"
		"\tdata.opacity = surface.opacity;\n"
		"\tdata.alphaClip = surface.alphaClip;\n"
		"}\n";
}
