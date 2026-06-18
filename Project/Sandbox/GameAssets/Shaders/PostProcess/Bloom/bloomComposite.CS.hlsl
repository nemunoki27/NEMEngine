#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	Texture
//============================================================================

// gSourceColorはブラー済みのbloom、元のシーン色はSRV名gSceneColorへSceneColorFinalを割り当てて受け取る
Texture2D<float4> gSceneColor : register(t2);

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float intensity; // bloomをシーンへ加算するときの倍率
};

//============================================================================
//	Main
//	Bloom多パスの最終段、ブラー済みbloomを元のシーンへ加算合成する
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint2 pixelPos = DTid.xy;
	if (IsOutside(pixelPos)) {
		return;
	}

	float3 bloom = LoadSource(pixelPos).rgb;
	float3 scene = gSceneColor.Load(int3(pixelPos, 0)).rgb;
	gDestColor[pixelPos] = float4(scene + bloom * intensity, 1.0f);
}
