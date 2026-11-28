#include "ShaderGraphStageSource.h"
#include "ShaderGraphRuntimeInputSource.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace {

	// GPU上の粒子と軌跡の配置を入力へ渡す
	std::string BuildGPUObjectInputSource(bool trail) {

		using namespace Engine::ShaderGraphStageSource;
		std::string source;
		if (trail) {
			source += BuildObjectInputAssignments("float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)", "0.0f.xxx", "0.0f.xxx");
			source += "\tuint2 segment = uint2(input.geometryIndex / pointCapacity, input.geometryIndex % pointCapacity);\n"
				"\tParticleTrailGPU trail = gGPUTrails[segment.x];\n"
				"\tParticleTrailPointGPU start = ReadParticleTrailPoint(trail, segment.x, segment.y);\n"
				"\tParticleTrailPointGPU end = ReadParticleTrailPoint(trail, segment.x, segment.y + 1u);\n"
				"\tfloat width = max(abs(startWidth), abs(endWidth)) * 0.5f;\n"
				"\tgraphInput.worldBoundsMin = min(start.positionAge.xyz, end.positionAge.xyz) - width;\n"
				"\tgraphInput.worldBoundsMax = max(start.positionAge.xyz, end.positionAge.xyz) + width;\n";
		} else {
			source += "\tParticleAttributesGPU particle = gGPUParticles[input.geometryIndex];\n"
				"\tParticleParentGPU parent = gGPUParents[particle.phase];\n"
				"\tfloat3 position;\n\tfloat4 rotation;\n\tfloat3 scale;\n"
				"\tResolveParticleGPUWorldTransform(particle, parent, position, rotation, scale);\n"
				"\tfloat4x4 world = float4x4(float4(ParticleRotate(float3(scale.x,0,0), rotation),0),\n"
				"\t\tfloat4(ParticleRotate(float3(0,scale.y,0), rotation),0), float4(ParticleRotate(float3(0,0,scale.z), rotation),0), float4(position,1));\n";
			source += BuildObjectInputAssignments("world", "0.0f.xxx", "0.0f.xxx");
			source += BuildSphereBoundsAssignments("0.0f.xxx", "geometryRadius", "world");
		}
		return source +
			"\tgraphInput.objectPosition = mul(float4(input.worldPosition, 1.0f), graphInput.worldToObject).xyz;\n"
			"\tgraphInput.objectNormal = normalize(mul(N, transpose((float3x3)graphInput.objectToWorld)));\n"
			"\tgraphInput.objectTangent = normalize(mul(T, (float3x3)graphInput.worldToObject));\n";
	}
}

std::string Engine::ShaderGraphStageSource::BuildGPUParticlePixelSource(
	const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile) {

	std::string source = "#include \"Builtin/Particle/GPU/particleGPUView.hlsli\"\n"
		"#include \"Builtin/Particle/GPU/particleGPUDraw.hlsli\"\n"
		"#include \"Builtin/Particle/Common/particleSurface.hlsli\"\n"
		"#include \"" + std::string(surfaceIncludeFile) + "\"\n"
		"#include \"Builtin/Particle/GPU/particleGPUMaterialCurves.hlsli\"\n"
		"Texture2D<float4> baseColorTexture : register(t0, space2);\n"
		"StructuredBuffer<ShaderGraphParameters> gParticleCustomParameters : register(t1, space1);\n\n"
		"ShaderGraphParameters GetParticleGPUParameters(float progress) {\n\n"
		"\tShaderGraphParameters result = gParticleCustomParameters[0];\n";
	if (graph.target == ShaderGraphTarget::Trail) {
		const std::string declarations = "#define NEM_TRAIL_CONSTANT_REGISTER b1\n"
			"#include \"Builtin/Particle/GPU/particleGPUTrail.hlsli\"\n"
			"#include \"Builtin/Particle/GPU/particleGPUTrailRead.hlsli\"\n";
		source.insert(0, declarations);
	}
	uint32_t offset = 0;
	uint32_t textureIndex = 0;
	for (const auto& parameter : graph.parameters) {
		const auto name = MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id);
		if (parameter.type == ShaderGraphValueType::Texture2D) {
			source += "\tresult." + name + " = " + std::to_string(textureIndex++) + "u;\n";
		} else if (parameter.type != ShaderGraphValueType::Matrix4 && parameter.type != ShaderGraphValueType::SamplerState) {
			const auto index = std::to_string(offset + 1);
			const auto count = ComponentCount(parameter.type);
			const std::string swizzle = count == 1 ? ".x" : count == 2 ? ".xy" : count == 3 ? ".xyz" : "";
			const auto sample = "SampleParticleGPURenderCurve(" + index + "u, progress).value0" + swizzle;
			const auto value = parameter.type == ShaderGraphValueType::Boolean ? "(" + sample + " != 0.0f)" : sample;
			source += "\tif (gParticleMaterialCurveHeaders[" + index + "].y != 0u) result." + name + " = (" +
				HLSLType(parameter.type) + ") " + value + ";\n";
		}
		offset += parameter.type == ShaderGraphValueType::Matrix4 ? 16 : (std::max)(ComponentCount(parameter.type), 1u);
	}
	for (const auto& keyword : graph.keywords) {
		if (!keyword.runtimeToggle) continue;
		const auto name = MakeIdentifier(keyword.referenceName.empty() ? keyword.name : keyword.referenceName, keyword.id);
		const auto index = std::to_string(offset++ + 1);
		const auto sample = "SampleParticleGPURenderCurve(" + index + "u, progress).value0.x";
		const auto value = keyword.type == ShaderGraphKeywordType::Boolean ? "(" + sample + " != 0.0f)" : sample;
		source += "\tif (gParticleMaterialCurveHeaders[" + index + "].y != 0u) result." + name +
			" = (" + std::string(keyword.type == ShaderGraphKeywordType::Boolean ? "uint" : "int") + ") " + value + ";\n";
	}
	source += "\treturn result;\n}\n\n"
		"ParticleGPUPixelOutput main(ParticleGPUVertexOutput input, bool isFrontFace : SV_IsFrontFace) {\n\n"
		"\tfloat progress = saturate(input.velocityAge.w / max(input.lifetimeRandom.x, 0.00001f));\n"
		"\tfloat3 N = ParticleSurfaceNormal(input.uv, input.normal, input.tangent, input.worldPosition);\n"
		"\tfloat3x3 tangentToWorld = ParticleSurfaceFrame(input.uv, input.normal, input.tangent, input.worldPosition);\n"
		"\tfloat3 T = tangentToWorld[0];\n"
		"\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput) 0;\n" +
		BuildFrameInputAssignments() +
		"\tgraphInput.uv = input.uv;\n"
		"\tgraphInput.worldPosition = input.worldPosition;\n"
		"\tgraphInput.worldNormal = N;\n"
		"\tgraphInput.isFrontFace = isFrontFace;\n" +
		BuildGPUObjectInputSource(graph.target == ShaderGraphTarget::Trail) +
		"\tgraphInput.viewDirection = normalize(particleCameraPosition.xyz - input.worldPosition);\n"
		"\tgraphInput.screenPosition = SceneRasterPositionToForward(input.position, shaderGraphReversedDepth);\n"
		"\tgraphInput.vertexColor = input.color;\n"
		"\tgraphInput.particleColor = input.particleColor;\n"
		"\tgraphInput.tangentToWorld = tangentToWorld;\n"
		"\tgraphInput.particleVelocity = input.velocityAge.xyz;\n"
		"\tgraphInput.particleAge = input.velocityAge.w;\n"
		"\tgraphInput.particleLifetime = input.lifetimeRandom.x;\n"
		"\tgraphInput.particleNormalizedAge = progress;\n"
		"\tgraphInput.particleRandom = input.lifetimeRandom.y;\n"
		"\tif (particleCustomDataAvailable != 0u) {\n"
		"\t\tParticleGPURenderCurveSample customData = SampleParticleGPURenderCurve(0u, progress);\n"
		"\t\tgraphInput.particleCustomData1 = customData.value0;\n"
		"\t\tgraphInput.particleCustomData2 = customData.value1;\n"
		"\t}\n"
		"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetParticleGPUParameters(progress));\n"
		"\tfloat4 color = baseColorTexture.Sample(gSampler, input.uv) * input.color * graph.baseColor;\n"
		"\tcolor.a *= graph.opacity;\n"
		"\tclip(color.a - max(input.alphaReference, graph.alphaClip));\n"
		"\tcolor.a *= ParticleSoftFade(input.position, input.worldPosition);\n"
		"\tcolor.rgb = ParticleSurfaceLighting(color.rgb, input.worldPosition, graph.normal,\n"
		"\t\tparticleSurfaceParameters.z, particleSurfaceParameters.w);\n"
		"\tcolor.rgb += graph.emissive + input.emissive.rgb * input.emissive.w;\n"
		"\tParticleGPUPixelOutput output;\n"
		"\toutput.color = PrepareParticleGPUBlend(color, input.blendMode, input.position);\n"
		"\treturn output;\n}\n";
	return source;
}
