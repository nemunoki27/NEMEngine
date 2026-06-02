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
	float gray = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
	float3 result = lerp(color.rgb, float3(gray, gray, gray), saturate(strength));
	gDestColor[pixelPos] = float4(result, color.a);
}
