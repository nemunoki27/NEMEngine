#ifndef NEM_MESH_SURFACE_LIGHTING_HLSLI
#define NEM_MESH_SURFACE_LIGHTING_HLSLI

//============================================================================
//	include
//============================================================================
#include "defaultMesh.hlsli"
#include "pbrShading.hlsli"

//============================================================================
//	Mesh surface lighting
//============================================================================
// 解決済みサーフェスへ全ライトと環境光を適用する
float3 EvaluateMeshSurfaceLighting(VSOutput input, ResolvedPBRMaterial material, float transmission = 0.0f) {

	uint instanceFlags = gMeshInstances[input.instanceID].flags;
	if ((instanceFlags & MESH_INSTANCE_FLAG_LIGHTING) == 0u) {
		return material.baseColor.rgb + material.emissive;
	}

	float3 V = normalize(renderCameraPos - input.worldPos);
	float3 F0 = lerp(0.04f.xxx, material.baseColor.rgb, material.metallic);
	float diffuseWeight = 1.0f - saturate(transmission);

	float3 lighting = 0.0f.xxx;
	[loop]
	for (uint i = 0; i < directionalCount; ++i) {
		lighting += EvaluatePBRDirectionalLight(
			gDirectionalLights[i], material.N, V,
			material.baseColor.rgb, material.metallic,
			material.roughness, F0, diffuseWeight);
	}
	[loop]
	for (uint i = 0; i < pointCount; ++i) {
		lighting += EvaluatePBRPointLight(
			gPointLights[i], input.worldPos, material.N, V,
			material.baseColor.rgb, material.metallic,
			material.roughness, F0, diffuseWeight);
	}
	[loop]
	for (uint i = 0; i < spotCount; ++i) {
		lighting += EvaluatePBRSpotLight(
			gSpotLights[i], input.worldPos, material.N, V,
			material.baseColor.rgb, material.metallic,
			material.roughness, F0, diffuseWeight);
	}
	[loop]
	for (uint i = 0; i < rectCount; ++i) {
		lighting += EvaluatePBRRectLight(
			gRectLights[i], input.worldPos, material.N, V,
			material.baseColor.rgb, material.metallic,
			material.roughness, F0, diffuseWeight);
	}

	float3 ambient = 0.0f.xxx;
	if ((instanceFlags & MESH_INSTANCE_FLAG_RECEIVE_IBL) != 0u) {
		ambient = 0.03f * material.baseColor.rgb * material.ao;
	}
	return lighting + ambient * diffuseWeight + material.emissive;
}

#endif // NEM_MESH_SURFACE_LIGHTING_HLSLI
