#ifndef NEM_ATMOSPHERE_AERIAL
#define NEM_ATMOSPHERE_AERIAL
#include "../Common/descriptorHeapCompatibility.hlsli"

float4 SampleAtmosphereAerial(uint textureIndex, SamplerState linearSampler, float2 screenUV,
	float distance, float maxDistance) {
	if (textureIndex == 0xffffffffu || distance <= 0.0f) return float4(0, 0, 0, 1);
	Texture2D<float4> lookup = NEM_TEXTURE2D(textureIndex);
	float slice = sqrt(saturate(distance / max(maxDistance, 0.001f))) * 32.0f - 0.5f;
	float lower = clamp(floor(slice), 0.0f, 31.0f);
	float upper = min(lower + 1.0f, 31.0f);
	float2 uv = clamp(screenUV, 0.5f / 32.0f, 31.5f / 32.0f);
	float4 a = lookup.SampleLevel(linearSampler, float2(uv.x, (uv.y + lower) / 32.0f), 0);
	float4 b = lookup.SampleLevel(linearSampler, float2(uv.x, (uv.y + upper) / 32.0f), 0);
	float4 result = lerp(a, b, saturate(slice - lower));
	return lerp(float4(0, 0, 0, 1), result, saturate((slice + 0.5f) * 2.0f));
}
#endif

