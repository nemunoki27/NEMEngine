#ifndef NEM_LIGHT_SHADER_DATA_HLSLI
#define NEM_LIGHT_SHADER_DATA_HLSLI

// 平行光源
struct DirectionalLight {

	float4 color;

	float3 direction;
	float intensity;

	float shadowStrength;
	float shadowAngularRadius;
	uint _alignmentPadding0;
	uint _alignmentPadding1;
};
// 点光源
struct PointLight {

	float4 color;

	float3 pos;
	float intensity;

	float radius;
	float decay;
	float shadowStrength;
	float shadowRadius;

	uint _alignmentPadding0;
	uint _alignmentPadding1;
	uint2 _pad1;
};
// スポットライト
struct SpotLight {

	float4 color;

	float3 direction;
	float intensity;

	float3 pos;
	float distance;

	float decay;
	float cosAngle;
	float cosFalloffStart;
	float shadowStrength;

	float shadowRadius;
	uint _alignmentPadding0;
	uint _alignmentPadding1;
	uint _pad0;
};
// 矩形面光源
struct RectLight {

	float4 color;

	float3 direction;
	float intensity;

	float3 pos;
	float attenuationRadius;

	float3 right;
	float sourceWidth;

	float3 up;
	float sourceHeight;

	float decay;
	float barnDoorAngle;
	float barnDoorLength;
	float shadowStrength;

	uint _alignmentPadding0;
	uint _alignmentPadding1;
	uint2 _pad0;
};
// ライト数
cbuffer LightCounts : register(b0) {

	uint directionalCount;
	uint pointCount;
	uint spotCount;
	uint rectCount;

	uint localCount;
	uint3 _lightCountPad;
};
StructuredBuffer<DirectionalLight> gDirectionalLights : register(t6);
StructuredBuffer<PointLight> gPointLights : register(t7);
StructuredBuffer<SpotLight> gSpotLights : register(t8);
StructuredBuffer<RectLight> gRectLights : register(t12);

#endif
