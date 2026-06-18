#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	Texture
//============================================================================

// SRV名gSceneNormalへ中間RTのSceneNormalMainを割り当ててGBuffer法線を受け取る
Texture2D<float4> gSceneNormal : register(t2);

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float edgeThreshold; // エッジとみなす隣接法線差の合計の閾値
	float edgeIntensity; // エッジの暗さ0..1
	float thickness;     // 隣接サンプルの間隔ピクセル
};

//============================================================================
//	Function
//============================================================================

float3 LoadNormal(int2 pixelPos) {

	return gSceneNormal.Load(int3(ClampPixel(pixelPos), 0)).xyz;
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

	int t = max((int)round(thickness), 1);
	int2 p = int2(pixelPos);

	// 中心と上下左右の法線差の合計をエッジ強度にする
	float3 n = LoadNormal(p);
	float3 nl = LoadNormal(p + int2(-t, 0));
	float3 nr = LoadNormal(p + int2(t, 0));
	float3 nu = LoadNormal(p + int2(0, -t));
	float3 nd = LoadNormal(p + int2(0, t));

	float diff =
		(1.0f - saturate(dot(n, nl))) +
		(1.0f - saturate(dot(n, nr))) +
		(1.0f - saturate(dot(n, nu))) +
		(1.0f - saturate(dot(n, nd)));

	float edge = step(edgeThreshold, diff);

	// エッジ部分だけシーン色を暗くして輪郭線にする
	float4 scene = LoadSource(pixelPos);
	float darken = 1.0f - saturate(edgeIntensity) * edge;
	gDestColor[pixelPos] = float4(scene.rgb * darken, scene.a);
}
