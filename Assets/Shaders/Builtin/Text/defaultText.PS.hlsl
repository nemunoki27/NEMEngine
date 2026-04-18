//============================================================================
//	include
//============================================================================

#include "defaultText.hlsli"

//============================================================================
//	output
//============================================================================

struct PSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	resources
//============================================================================

Texture2D<float4> gAtlas : register(t1);
SamplerState gSampler : register(s0);

struct PSInstance {

	float4 color;
	float2 atlasSize;
	float pxRange;
	float padding;
};
StructuredBuffer<PSInstance> gPSInstances : register(t2);

//============================================================================
//	functions
//============================================================================

// RGBの中央値を計算する関数
float Median(float r, float g, float b) {

	return max(min(r, g), min(max(r, g), b));
}

// スクリーン上のピクセル距離を計算する関数
float ComputeScreenPxRange(float2 uv, float pxRange, float2 atlasSize) {

	float2 unitRange = float2(pxRange / atlasSize.x, pxRange / atlasSize.y);
	float2 screenTexSize = rcp(fwidth(uv));
	return max(0.5f * dot(unitRange, screenTexSize), 1.0f);
}

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	PSInstance instance = gPSInstances[input.instanceID];

	// テクスチャからMSDFをサンプリング
	float3 msdf = gAtlas.Sample(gSampler, input.texcoord).rgb;
	float signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5f;
	float screenPxDistance = ComputeScreenPxRange(input.texcoord, instance.pxRange, instance.atlasSize) * signedDistance;
	float alpha = saturate(screenPxDistance + 0.5f);

	PSOutput output;
	
	// 色を設定
	output.color = float4(instance.color.rgb, instance.color.a * alpha);

	return output;
}