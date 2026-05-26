#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float threshold;
	float knee;
	float intensity;
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
	float luminance = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
	float soft = saturate((luminance - threshold + knee) / max(knee * 2.0f, 0.0001f));
	float contribution = max(luminance - threshold, 0.0f) + soft * soft * knee;
	float scale = contribution / max(luminance, 0.0001f);

	gDestColor[pixelPos] = float4(color.rgb * scale * intensity, 1.0f);
}
