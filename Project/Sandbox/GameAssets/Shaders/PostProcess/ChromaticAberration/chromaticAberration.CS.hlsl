#include "../Common/PostProcessCommon.hlsli"

//============================================================================
//  CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float strength;
	float falloff;
	float2 center;
};

SamplerState gLinearClampSampler : register(s0);

//============================================================================
//  Main
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint2 pixelPos = DTid.xy;
	if (IsOutside(pixelPos)) {
		return;
	}

	uint width, height;
	gDestColor.GetDimensions(width, height);

	float2 screenSize = float2(width, height);
	float2 invScreenSize = 1.0f / screenSize;

	// 現在ピクセルのUV
	float2 uv = (float2(pixelPos) + 0.5f) * invScreenSize;

	// 画面中心からの方向
	float2 fromCenter = uv - center;

	// アスペクト比補正
	float aspect = screenSize.x / screenSize.y;
	float2 aspectCorrected = float2(fromCenter.x * aspect, fromCenter.y);

	float dist = length(aspectCorrected);
	float dist01 = saturate(dist / 0.7071f);

	float2 dir = float2(0.0f, 0.0f);
	if (dist > 0.0001f) {
		dir = fromCenter / length(fromCenter);
	}

	// ピクセル単位のズレ量
	float amountPixel = strength * pow(dist01, falloff);

	// UV単位に変換
	float2 offsetUV = dir * amountPixel * invScreenSize;

	float4 baseColor = gSourceColor.SampleLevel(gLinearClampSampler, uv, 0.0f);

	float r = gSourceColor.SampleLevel(gLinearClampSampler, uv + offsetUV, 0.0f).r;
	float g = baseColor.g;
	float b = gSourceColor.SampleLevel(gLinearClampSampler, uv - offsetUV, 0.0f).b;

	gDestColor[pixelPos] = float4(r, g, b, baseColor.a);
}