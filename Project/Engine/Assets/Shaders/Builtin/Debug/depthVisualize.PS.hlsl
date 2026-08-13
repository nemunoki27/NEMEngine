//============================================================================
//	include
//============================================================================
#include "../FullscreenCopy/fullscreenCopy.hlsli"

//============================================================================
//	resources
//============================================================================
Texture2D<float> gDepth : register(t0);

//============================================================================
//	main
//============================================================================
float4 main(VSOutput input) : SV_TARGET0 {

	uint width = 0;
	uint height = 0;
	gDepth.GetDimensions(width, height);

	const uint2 pixel = min(
		uint2(input.texcoord * float2(width, height)),
		uint2(width - 1, height - 1));
	const float depth = saturate(gDepth.Load(int3(pixel, 0)));
	return float4(depth.xxx, 1.0f);
}
