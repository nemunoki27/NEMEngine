#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float intensity;
	float lineDensity;
	float speed;
	float thickness;

	float4 lineColor;
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

	float4 sourceColor = LoadSource(pixelPos);
	float2 uv = (float2(pixelPos) + 0.5f) * invResolution;
	float density = max(lineDensity, 1.0f);
	float phase = frac(uv.y * density + time * speed);
	float distanceToLine = abs(phase - 0.5f);
	float halfWidth = saturate(thickness) * 0.5f;
	float antiAliasWidth = max(density * invResolution.y, 0.0001f);
	float lineMask = 1.0f - smoothstep(
		halfWidth, halfWidth + antiAliasWidth, distanceToLine);
	float amount = lineMask * saturate(intensity) * saturate(lineColor.a);

	float3 result = lerp(sourceColor.rgb,
		sourceColor.rgb * max(lineColor.rgb, 0.0f), amount);
	gDestColor[pixelPos] = float4(result, sourceColor.a);
}
