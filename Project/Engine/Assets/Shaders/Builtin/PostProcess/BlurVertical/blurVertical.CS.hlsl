#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float radius;
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

	int r = clamp((int)round(radius), 1, 8);
	float4 accum = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float weightSum = 0.0f;

	[loop]
	for (int y = -r; y <= r; ++y) {

		float weight = 1.0f - (abs((float)y) / ((float)r + 1.0f));
		accum += LoadSourceClamp(int2(pixelPos) + int2(0, y)) * weight;
		weightSum += weight;
	}
	gDestColor[pixelPos] = accum / max(weightSum, 0.0001f);
}
