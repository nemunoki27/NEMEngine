#ifndef NEM_TEMPORAL_MOTION_HLSLI
#define NEM_TEMPORAL_MOTION_HLSLI

float2 ComputeScreenMotion(float4 currentClip, float4 previousClip) {

	if (currentClip.w <= 0.0f || previousClip.w <= 0.0f) return 0.0f.xx;
	const float2 currentUV = currentClip.xy / currentClip.w * float2(0.5f, -0.5f) + 0.5f;
	const float2 previousUV = previousClip.xy / previousClip.w * float2(0.5f, -0.5f) + 0.5f;
	return currentUV - previousUV;
}

#endif
