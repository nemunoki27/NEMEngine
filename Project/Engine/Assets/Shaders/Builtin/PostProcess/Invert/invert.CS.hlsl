#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float strength;
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
	float3 inverted = float3(1.0f, 1.0f, 1.0f) - color.rgb;
	gDestColor[pixelPos] = float4(lerp(color.rgb, inverted, saturate(strength)), color.a);
}
