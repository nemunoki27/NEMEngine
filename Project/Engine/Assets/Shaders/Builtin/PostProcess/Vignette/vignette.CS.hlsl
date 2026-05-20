#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float intensity;
	float radius;
	float softness;
};

//============================================================================
//	Main
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint2 pixelPos = DTid.xy;
	if (IsOutside(pixelPos)) {
		return;
	}

	float4 color = LoadSource(pixelPos);
	float2 uv = (float2(pixelPos) + float2(0.5f, 0.5f)) * invResolution;
	float dist = distance(uv, float2(0.5f, 0.5f));
	float edge0 = max(radius - softness, 0.0001f);
	float edge1 = max(radius, edge0 + 0.0001f);
	float vignette = smoothstep(edge0, edge1, dist);
	color.rgb *= lerp(1.0f, 1.0f - saturate(intensity), vignette);
	gDestColor[pixelPos] = color;
}
