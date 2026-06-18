#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	Texture
//============================================================================

// 深度はSceneFinalには無いため、SRV名gSceneDepthへ中間RTのSceneDepthを割り当てて受け取る
Texture2D<float> gSceneDepth : register(t2);

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float3 fogColor; // 遠景がにじむフォグの色
	float fogStart;  // フォグが掛かり始める線形深度[0,1]でnear..farを0..1に正規化した値
	float fogEnd;    // フォグが最大になる線形深度[0,1]
	float fogMaxAmount; // 最遠でのフォグ適用率の上限0..1
};

//============================================================================
//	Function
//============================================================================

float LoadSceneDepth(uint2 pixelPos) {

	return gSceneDepth.Load(int3(pixelPos, 0));
}

//============================================================================
//	Main
//	深度が遠いほどシーン色をフォグ色へ近づける距離フォグ
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint2 pixelPos = DTid.xy;
	if (IsOutside(pixelPos)) {
		return;
	}

	// 非線形深度を線形化してから、fogStart..fogEndでフォグ率を立ち上げる、手前はクリアのまま
	float depth = LinearizeDepth01(LoadSceneDepth(pixelPos));
	float t = saturate((depth - fogStart) / max(fogEnd - fogStart, 0.0001f));
	t *= saturate(fogMaxAmount);

	float4 scene = LoadSource(pixelPos);
	float3 result = lerp(scene.rgb, fogColor, t);
	gDestColor[pixelPos] = float4(result, scene.a);
}
