#include "../Common/PostProcessCommon.hlsli"
#include "../Common/PostProcessDepth.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float blurStartDistance;
	float blurTransition;
	float blurStrength;
	float maxBlurRadius;
	int sampleCount;
	float3 _padding;
};

//============================================================================
//	Function
//============================================================================

static const float GOLDEN_ANGLE = 2.39996323f;

float LinearizeEyeDepth(float rawDepth) {

	return (cameraFar * cameraNear) /
		max(cameraFar - rawDepth * (cameraFar - cameraNear), 0.000001f);
}

float CalculateBlurAmount(float eyeDepth) {

	float distanceFromStart = eyeDepth - max(blurStartDistance, 0.0f);
	return saturate(distanceFromStart /
		max(blurTransition, 0.0001f)) * saturate(blurStrength);
}

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
	float eyeDepth = LinearizeEyeDepth(LoadSourceDepth(pixelPos));
	float centerBlur = CalculateBlurAmount(eyeDepth);
	float blurRadius = centerBlur * max(maxBlurRadius, 0.0f);
	if (blurRadius < 0.5f) {
		gDestColor[pixelPos] = sourceColor;
		return;
	}

	int count = clamp(sampleCount, 4, 16);
	float3 colorSum = sourceColor.rgb;
	float weightSum = 1.0f;
	[loop]
	for (int index = 0; index < count; ++index) {

		float sampleRatio = ((float)index + 0.5f) / (float)count;
		float sampleRadius = sqrt(sampleRatio) * blurRadius;
		float angle = (float)index * GOLDEN_ANGLE;
		int2 offset = int2(round(float2(cos(angle), sin(angle)) *
			sampleRadius));
		int2 samplePosition = ClampPixel(int2(pixelPos) + offset);
		float sampleDepth = LinearizeEyeDepth(
			LoadSourceDepth(uint2(samplePosition)));
		float sampleBlur = CalculateBlurAmount(sampleDepth);
		float sampleWeight = lerp(0.25f, 1.0f,
			saturate(max(centerBlur, sampleBlur)));

		colorSum += LoadSourceClamp(samplePosition).rgb * sampleWeight;
		weightSum += sampleWeight;
	}

	gDestColor[pixelPos] = float4(colorSum / max(weightSum, 0.0001f),
		sourceColor.a);
}
