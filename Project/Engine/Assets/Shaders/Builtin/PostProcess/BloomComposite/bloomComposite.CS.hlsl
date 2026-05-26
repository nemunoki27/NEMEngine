#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	Texture
//============================================================================

Texture2D<float4> gBloomTexture : register(t2);

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float intensity;
	float3 padding0;
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

	float4 sceneColor = LoadSource(pixelPos);
	float4 bloomColor = gBloomTexture.Load(int3(pixelPos, 0));
	gDestColor[pixelPos] = float4(sceneColor.rgb + bloomColor.rgb * intensity, sceneColor.a);
}
