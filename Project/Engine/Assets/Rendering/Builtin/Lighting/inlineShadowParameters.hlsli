#ifndef NEM_INLINE_SHADOW_PARAMETERS_HLSLI
#define NEM_INLINE_SHADOW_PARAMETERS_HLSLI

cbuffer InlineShadowParameters : register(b3) {

	uint2 shadowSize;
	uint shadowLightCount;
	uint shadowFrameIndex;

	uint softShadowSampleCount;
	uint resetShadowHistory;
	uint shadowFilterStep;
	uint shadowPreviewLightIndex;

	float shadowNormalBias;
	float shadowMaxDistance;
	float shadowProjectionScale;
	float shadowOrthographic;

	float4x4 shadowViewMatrix;
	float4x4 shadowPreviousViewProjection;
};

uint GetShadowOffset(uint2 pixel, uint slot) {

	return (slot * shadowSize.y + pixel.y) * shadowSize.x + pixel.x;
}

float GetShadowPixelSize(float3 position) {

	float depth = shadowOrthographic != 0.0f ? 1.0f : max(mul(float4(position, 1.0f), shadowViewMatrix).z, 0.01f);
	return max(depth / shadowProjectionScale, 0.0001f);
}

// 半影の幅と積算回数を同じ要素へ保持
float PackShadowHistory(float radius, float count) {

	return floor(clamp(radius, 0.0f, 32.0f) * 16.0f + 0.5f) * 32.0f + clamp(count, 1.0f, 16.0f);
}

float GetShadowHistoryRadius(float history) {

	return floor(history / 32.0f) / 16.0f;
}

float GetShadowHistoryCount(float history) {

	return float(uint(history) & 31u);
}

// 遮蔽率だけを量子化し、半影幅と積算回数は整数で保持
uint EncodeShadowValue(float2 value) {

	return uint(saturate(value.x) * 65535.0f + 0.5f) | (uint(value.y) << 16u);
}

float2 DecodeShadowValue(uint value) {

	return float2(float(value & 65535u) / 65535.0f, float(value >> 16u));
}

#endif
