#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float threshold; // 抽出を始める輝度の下限
	float knee;      // 閾値付近をなめらかに立ち上げる幅
	float intensity; // 抽出した輝度に掛ける倍率
};

//============================================================================
//	Function
//============================================================================

static const float3 LUMA = float3(0.2125f, 0.7154f, 0.0721f);

//============================================================================
//	Main
//	Bloom多パスの1段目、閾値以上の輝度だけを取り出す
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint2 pixelPos = DTid.xy;
	if (IsOutside(pixelPos)) {
		return;
	}

	float3 color = LoadSource(pixelPos).rgb;
	float lum = dot(color, LUMA);

	// thresholdからknee幅でフェードインさせ、境界のちらつきを抑える
	float soft = smoothstep(threshold, threshold + max(knee, 0.0001f), lum);
	gDestColor[pixelPos] = float4(color * soft * intensity, 1.0f);
}
