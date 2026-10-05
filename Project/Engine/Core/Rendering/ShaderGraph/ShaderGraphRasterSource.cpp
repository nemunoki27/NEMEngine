#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// 色と輪郭のShaderを生成する
	std::string BuildPrimitivePixelSource(
		std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context, bool transparent) {

		std::string source = "// Shader Graph generated file\n"
							 "#include \"Builtin/Primitive/primitive.hlsli\"\n"
							 "#include \"Builtin/Mesh/Common/pbrShading.hlsli\"\n"
							 "#include \"Builtin/Mesh/Common/deferredGBuffer.hlsli\"\n"
							 "#include \"" +
							 std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source +=
			"\nShaderGraphSurface EvaluatePrimitiveShaderGraph(VSOutput input) {\n\n"
			"\tfloat3 N = normalize(input.normal);\n"
			"\tfloat3 T = normalize(input.tangent - N * dot(N, input.tangent));\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = ResolvePrimitivePixelUV(input.texcoord, input.uvCoordinates, input.ringParams, input.uvBasis);\n"
			"\tgraphInput.worldNormal = N;\n"
			"\tgraphInput.worldPosition = input.worldPos;\n"
			"\tgraphInput.objectPosition = input.worldPos;\n"
			"\tgraphInput.objectNormal = input.normal;\n"
			"\tgraphInput.objectTangent = T;\n"
			"\tgraphInput.viewDirection = normalize(cameraPosition - input.worldPos);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(T, cross(N, T) * input.tangentSign, N);\n"
			"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters());\n"
			"\tgraph.baseColor *= input.vertexColor;\n"
			"\treturn graph;\n"
			"}\n\n";
		if (!transparent) {
			source += "GBufferOutput main(VSOutput input) {\n\n"
					  "\tShaderGraphSurface graph = EvaluatePrimitiveShaderGraph(input);\n"
					  "\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
					  "\tMeshSurface surface;\n"
					  "\tsurface.albedo = graph.baseColor.rgb;\n"
					  "\tsurface.normal = graph.normal;\n"
					  "\tsurface.worldPos = input.worldPos;\n"
					  "\tsurface.metallic = graph.metallic;\n"
					  "\tsurface.roughness = graph.roughness;\n"
					  "\tsurface.occlusion = graph.ambientOcclusion;\n"
					  "\tsurface.emissive = graph.emissive;\n"
					  "\tsurface.motion = ComputeGBufferMotion(input.currentClipPosition, input.previousClipPosition);\n"
					  "\tsurface.flags = BuildMaterialFlags(input.flags);\n"
					  "\treturn EncodeGBuffer(surface);\n"
					  "}\n";
		} else {
			source += "struct TransparentPSOutput { float4 color : SV_TARGET0; };\n\n"
					  "TransparentPSOutput mainTransparent(VSOutput input) {\n\n"
					  "\tShaderGraphSurface graph = EvaluatePrimitiveShaderGraph(input);\n"
					  "\tfloat alpha = graph.baseColor.a * graph.opacity;\n"
					  "\tclip(alpha - graph.alphaClip);\n"
					  "\tfloat3 V = normalize(cameraPosition - input.worldPos);\n"
					  "\tfloat3 F0 = lerp(0.04f.xxx, graph.baseColor.rgb, graph.metallic);\n"
					  "\tfloat3 lighting = 0.0f.xxx;\n"
					  "\t[loop] for (uint i = 0; i < directionalCount; ++i) { lighting += "
					  "EvaluatePBRDirectionalLight(gDirectionalLights[i], graph.normal, V, graph.baseColor.rgb, "
					  "graph.metallic, graph.roughness, F0); }\n"
					  "\t[loop] for (uint i = 0; i < pointCount; ++i) { lighting += EvaluatePBRPointLight(gPointLights[i], "
					  "input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0); }\n"
					  "\t[loop] for (uint i = 0; i < spotCount; ++i) { lighting += EvaluatePBRSpotLight(gSpotLights[i], "
					  "input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0); }\n"
					  "\t[loop] for (uint i = 0; i < rectCount; ++i) { lighting += EvaluatePBRRectLight(gRectLights[i], "
					  "input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0); }\n"
					  "\tfloat3 ambient = ((input.flags & MESH_INSTANCE_FLAG_RECEIVE_IBL) != 0u) ? 0.03f * graph.baseColor.rgb "
					  "* graph.ambientOcclusion : 0.0f.xxx;\n"
					  "\tTransparentPSOutput output;\n"
					  "\toutput.color = float4(lighting + ambient + graph.emissive, alpha);\n"
					  "\treturn output;\n"
					  "}\n";
		}
		return source;
	}

	// 色と輪郭のShaderを生成する
	std::string BuildUnlitPixelSource(
		ShaderGraphTarget target, std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context) {

		const bool sprite = target == ShaderGraphTarget::Sprite;
		const bool text = target == ShaderGraphTarget::Text;
		std::string source = "// Shader Graph generated file\n";
		source += sprite ? "#include \"Builtin/Sprite/defaultSprite.hlsli\"\n"
						 : (text ? "#include \"Builtin/Text/defaultText.hlsli\"\n"
								 : "#include \"Builtin/Primitive/primitive2D.hlsli\"\n");
		source += "SamplerState gSampler : register(s0);\n";
		if (text) {
			source += "Texture2D<float4> gAtlas : register(t1);\n"
					  "struct PSInstance { float2 atlasSize; float pxRange; float padding0; float4x4 uvMatrix; };\n"
					  "StructuredBuffer<PSInstance> gPSInstances : register(t2);\n";
		} else if (sprite) {
			source += "struct PSInstance { float4x4 uvMatrix; };\n"
					  "StructuredBuffer<PSInstance> gPSInstances : register(t2);\n";
		}
		source += "#include \"" + std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		if (text) {
			source += "\nfloat Median(float r, float g, float b) { return max(min(r, g), min(max(r, g), b)); }\n"
					  "float ComputeScreenPxRange(float2 uv, float pxRange, float2 atlasSize) {\n"
					  "\tfloat2 unitRange = float2(pxRange / atlasSize.x, pxRange / atlasSize.y);\n"
					  "\treturn max(0.5f * dot(unitRange, rcp(fwidth(uv))), 1.0f);\n"
					  "}\n";
		}
		source += "\nstruct PSOutput { float4 color : SV_TARGET0; };\n\n"
				  "PSOutput main(VSOutput input) {\n\n"
				  "\tShaderGraphSurfaceInput graphInput;\n";
		if (sprite) {
			source += "\tPSInstance instance = gPSInstances[input.instanceID];\n"
					  "\tgraphInput.uv = mul(float4(input.texcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n";
		} else if (text) {
			source += "\tPSInstance instance = gPSInstances[input.instanceID];\n"
					  "\tgraphInput.uv = mul(float4(input.materialTexcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n";
		} else {
			source += "\tgraphInput.uv = ResolvePrimitivePixelUV(input.texcoord, input.uvCoordinates, input.ringParams, "
					  "input.uvBasis);\n";
		}
		source += "\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.worldPosition = float3(0.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.objectPosition = float3(0.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.screenPosition = input.position;\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
				  "\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters());\n"
				  "\tfloat alpha = graph.baseColor.a * graph.opacity;\n";
		if (text) {
			source += "\tfloat3 msdf = gAtlas.Sample(gSampler, input.texcoord).rgb;\n"
					  "\tfloat signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5f;\n"
					  "\tfloat coverage = saturate(ComputeScreenPxRange(input.texcoord, instance.pxRange, instance.atlasSize) "
					  "* signedDistance + 0.5f);\n"
					  "\talpha *= coverage;\n";
		}
		source += "\tclip(alpha - max(graph.alphaClip, 1.0f / 255.0f));\n"
				  "\tPSOutput output;\n"
				  "\toutput.color = float4(graph.baseColor.rgb, alpha);\n"
				  "\treturn output;\n"
				  "}\n";
		return source;
	}

	// 色と輪郭のShaderを生成する
	std::string BuildParticlePixelSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile) {

		std::string source =
			"// Shader Graph generated Particle file\n"
			"#include \"Builtin/Particle/Common/particle.hlsli\"\n"
			"Texture2D<float4> baseColorTexture : register(t0, space2);\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" +
			std::string(surfaceIncludeFile) +
			"\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gParticleCustomParameters : register(t1, space1);\n\n"
			"ShaderGraphParameters GetParticleShaderGraphParameters(VSOutput input) {\n\n"
			"\tShaderGraphParameters result = (ShaderGraphParameters) 0;\n"
			"\tif (input.particleIndex != 0xffffffffu) result = gParticleCustomParameters[input.particleIndex];\n";
		uint32_t textureIndex = 0;
		for (const ShaderGraphParameter& parameter : graph.parameters) {
			if (parameter.type != ShaderGraphValueType::Texture2D) { continue; }
			source += "\tresult." +
					  MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id) +
					  " = " + std::to_string(textureIndex++) + "u;\n";
		}
		source +=
			"\treturn result;\n"
			"}\n\n"
			"struct PSOutput { float4 color : SV_TARGET0; };\n\n"
			"PSOutput main(VSOutput input) {\n\n"
			"\tParticleMaterialData material = GetParticleMaterial(input);\n"
			"\tfloat2 uv = TransformParticleUV(input.texcoord, material);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = uv;\n"
			"\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.worldPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = input.vertexColor;\n"
			"\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
			"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetParticleShaderGraphParameters(input));\n"
			"\tfloat4 color = baseColorTexture.Sample(gSampler, uv) * input.vertexColor * material.materialColor * "
			"graph.baseColor;\n"
			"\tcolor.a *= graph.opacity;\n"
			"\tclip(color.a - max(material.materialParams.x, graph.alphaClip));\n"
			"\tcolor.rgb += graph.emissive + material.emissive.rgb * material.emissive.w;\n"
			"\tcolor = PrepareParticleBlendColor(color, material);\n"
			"\tPSOutput output;\n"
			"\toutput.color = color;\n"
			"\treturn output;\n"
			"}\n";
		return source;
	}

	// 色と輪郭のShaderを生成する
	std::string BuildUnlitOutlinePixelSource(
		ShaderGraphTarget target, std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context) {

		const bool sprite = target == ShaderGraphTarget::Sprite;
		std::string source = "// Shader Graph generated Outline file\n";
		source +=
			sprite ? "#include \"Builtin/Sprite/defaultSprite.hlsli\"\n" : "#include \"Builtin/Primitive/primitive2D.hlsli\"\n";
		source += "#include \"Builtin/ScreenSpaceOutline/Common/screenSpaceOutlineCommon.hlsli\"\n"
				  "SamplerState gSampler : register(s0);\n";
		if (sprite) {
			source += "struct PSInstance { float4x4 uvMatrix; };\n"
					  "StructuredBuffer<PSInstance> gPSInstances : register(t2);\n";
		}
		source += "#include \"" + std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\ncbuffer ScreenSpaceOutlineMaskConstantsBuffer : register(b1, space1) {\n\n"
				  "\tScreenSpaceOutlineMaskConstants gMaskConstants;\n"
				  "};\n\n"
				  "uint main(VSOutput input) : SV_Target0 {\n\n"
				  "\tif (gMaskConstants.styleID == 0u) { discard; }\n"
				  "\tShaderGraphSurfaceInput graphInput;\n";
		if (sprite) {
			source += "\tPSInstance instance = gPSInstances[input.instanceID];\n"
					  "\tgraphInput.uv = mul(float4(input.texcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n";
		} else {
			source += "\tgraphInput.uv = ResolvePrimitivePixelUV(input.texcoord, input.uvCoordinates, input.ringParams, "
					  "input.uvBasis);\n";
		}
		source += "\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.worldPosition = float3(0.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.objectPosition = float3(0.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
				  "\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
				  "\tgraphInput.screenPosition = input.position;\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
				  "\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters());\n"
				  "\tfloat alpha = graph.baseColor.a * graph.opacity;\n"
				  "\tclip(alpha - max(graph.alphaClip, gMaskConstants.alphaThreshold));\n"
				  "\treturn gMaskConstants.styleID;\n"
				  "}\n";
		return source;
	}

	// 色と輪郭のShaderを生成する
	std::string BuildPixelSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile,
		const ShaderGraphExpressionCompiler& context, bool transparent) {

		switch (graph.target) {
		case ShaderGraphTarget::Mesh:
			return BuildMeshPixelSource(surfaceIncludeFile, transparent);
		case ShaderGraphTarget::Primitive3D:
			return BuildPrimitivePixelSource(surfaceIncludeFile, context, transparent);
		case ShaderGraphTarget::Sprite:
		case ShaderGraphTarget::Text:
		case ShaderGraphTarget::Primitive2D:
			return BuildUnlitPixelSource(graph.target, surfaceIncludeFile, context);
		case ShaderGraphTarget::Particle:
		case ShaderGraphTarget::Trail:
			return BuildParticlePixelSource(graph, surfaceIncludeFile);
		}
		return {};
	}

} // Engine::ShaderGraphStageSource
