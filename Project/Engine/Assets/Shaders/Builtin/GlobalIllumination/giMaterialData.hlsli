#ifndef NEM_GI_MATERIAL_DATA
#define NEM_GI_MATERIAL_DATA

struct GIMaterialData {

	uint parametersDescriptor;
	float2 uv;
	float4 vertexColor;
	float3 worldPosition;
	float3 worldNormal;
	float3 objectPosition;
	float3 objectNormal;
	float3 objectTangent;
	float3 viewDirection;
	float4 screenPosition;
	float3x3 tangentToWorld;
	float4 baseColor;
	float3 normal;
	float metallic;
	float roughness;
	float ambientOcclusion;
	float3 emissive;
	float opacity;
	float alphaClip;
};

#endif
