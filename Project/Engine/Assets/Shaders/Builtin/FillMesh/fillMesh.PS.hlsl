//============================================================================
//	include
//============================================================================
#include "fillMesh.hlsli"
#include "../Mesh/Common/pbrShading.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"

//============================================================================
//	resources
//============================================================================
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float3 cameraPosition;
	float _viewPadding;
};
cbuffer ObjectConstants : register(b1) {

	float4x4 worldMatrix;
	float4 objectColor;
	uint renderingLayerMask;
	float3 _objectPadding;
};
cbuffer MaterialParameters : register(b3) {

	float4 color;
	float4 emissiveColor;

	float Metallic;
	float Roughness;
	float ambientOcclusion;
	float emissiveIntensity;
};

//============================================================================
//	output
//============================================================================
struct TransparentPSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	material
//============================================================================
ResolvedPBRMaterial ResolveFillMeshMaterial(VSOutput input) {

	ResolvedPBRMaterial material;
	material.baseColor = objectColor * color;
	material.N = normalize(input.normal);
	material.metallic = saturate(Metallic);
	material.roughness = max(saturate(Roughness), 0.04f);
	material.ao = saturate(ambientOcclusion);
	material.emissive = emissiveColor.rgb * max(emissiveIntensity, 0.0f);
	return material;
}

float3 EvaluateFillMeshLighting(VSOutput input, ResolvedPBRMaterial material) {

	float3 V = normalize(cameraPosition - input.worldPos);
	float3 F0 = lerp(0.04f.xxx, material.baseColor.rgb, material.metallic);
	float3 lighting = 0.0f.xxx;

	[loop]
	for (uint i = 0; i < directionalCount; ++i) {
		lighting += EvaluatePBRDirectionalLight(gDirectionalLights[i],
			material.N, V, material.baseColor.rgb, material.metallic,
			material.roughness, F0);
	}
	[loop]
	for (uint i = 0; i < pointCount; ++i) {
		lighting += EvaluatePBRPointLight(gPointLights[i], input.worldPos,
			material.N, V, material.baseColor.rgb, material.metallic,
			material.roughness, F0);
	}
	[loop]
	for (uint i = 0; i < spotCount; ++i) {
		lighting += EvaluatePBRSpotLight(gSpotLights[i], input.worldPos,
			material.N, V, material.baseColor.rgb, material.metallic,
			material.roughness, F0);
	}
	[loop]
	for (uint i = 0; i < rectCount; ++i) {
		lighting += EvaluatePBRRectLight(gRectLights[i], input.worldPos,
			material.N, V, material.baseColor.rgb, material.metallic,
			material.roughness, F0);
	}

	const float3 ambient =
		0.03f * material.baseColor.rgb * material.ao;
	return lighting + ambient + material.emissive;
}

//============================================================================
//	main
//============================================================================
GBufferOutput main(VSOutput input) {

	const ResolvedPBRMaterial material = ResolveFillMeshMaterial(input);

	MeshSurface surface;
	surface.albedo = material.baseColor.rgb;
	surface.normal = material.N;
	surface.worldPos = input.worldPos;
	surface.metallic = material.metallic;
	surface.roughness = material.roughness;
	surface.occlusion = material.ao;
	surface.emissive = material.emissive;
	surface.flags = kMaterialFlagLightingDefault |
		PackRenderingLayerMask(
			renderingLayerMask);

	GBufferOutput output = EncodeGBuffer(surface);
	output.albedo.a = material.baseColor.a;
	return output;
}

//============================================================================
//	mainTransparent
//============================================================================
TransparentPSOutput mainTransparent(VSOutput input) {

	const ResolvedPBRMaterial material = ResolveFillMeshMaterial(input);

	TransparentPSOutput output;
	output.color = float4(EvaluateFillMeshLighting(input, material),
		material.baseColor.a);
	return output;
}
