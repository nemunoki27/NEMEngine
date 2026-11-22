//============================================================================
//	include
//============================================================================
#include "defaultText.hlsli"
#include "../Atmosphere/transparentFog.hlsli"

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
Texture2D<float4> baseColorTexture : register(t0, space2);

cbuffer MaterialParameters : register(b3) {

	float4 color;
	bool enableOutline;
	float4 outlineColor;
	float outlineWidth;
};

//============================================================================
//	functions
//============================================================================
// RGBの中央値を計算する
float Median(float r, float g, float b) {

	return max(min(r, g), min(max(r, g), b));
}

// スクリーン上のピクセル距離を計算する
float ComputeScreenPxRange(float2 uv, float pxRange, float2 atlasSize) {

	float2 unitRange = float2(pxRange / atlasSize.x, pxRange / atlasSize.y);
	float2 screenTexSize = rcp(fwidth(uv));
	return max(0.5f * dot(unitRange, screenTexSize), 1.0f);
}

//============================================================================
//	main
//============================================================================
PSOutput EvaluateTextColor(VSOutput input, PSInstance instance) {

	// テクスチャからMSDFをサンプリング
	float3 msdf = gAtlas.Sample(gSampler, input.texcoord).rgb;
	float4 transformedUV = mul(float4(input.materialTexcoord, 0.0f, 1.0f), instance.uvMatrix);
	float4 fillColor = baseColorTexture.Sample(gSampler, transformedUV.xy) * color;
	float signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5f;
	float screenPxDistance = ComputeScreenPxRange(input.texcoord, instance.pxRange, instance.atlasSize) * signedDistance;

	// 文字本体のカバレッジ
	float fillAlpha = saturate(screenPxDistance + 0.5f);

	PSOutput output;

	// アウトライン有効時は本体より広い距離でカバレッジを取り、縁を別色で塗る
	if (enableOutline && outlineWidth > 0.0f) {

		float pixelScale = min(referenceToRenderScale.x, referenceToRenderScale.y);
		float outerAlpha = saturate(screenPxDistance + outlineWidth * pixelScale + 0.5f);
		// 縁はoutlineColor、内側へ向かって本体色へ補間する
		float3 rgb = lerp(outlineColor.rgb, fillColor.rgb, fillAlpha);
		float regionAlpha = lerp(outlineColor.a, fillColor.a, fillAlpha);
		output.color = float4(rgb, outerAlpha * regionAlpha);
	} else {
		output.color = float4(fillColor.rgb, fillAlpha * fillColor.a);
	}
	output.color *= instance.charColor;
	// グリフ外の透明ピクセルには深度を書き込まない
	clip(output.color.a - 1.0f / 255.0f);
	return output;
}

uint4 PickText(VSOutput input) : SV_TARGET0 {

	PSInstance instance = gPSInstances[input.instanceID];
	EvaluateTextColor(input, instance);
	return uint4(instance.entityIndex, instance.entityGeneration, 0u, 1u);
}

PSOutput main(VSOutput input) {

	PSInstance instance = gPSInstances[input.instanceID];
	PSOutput output = EvaluateTextColor(input, instance);
	output.color = ApplyShineHighlight(output.color, input.layoutTexcoord, instance.shine);
	output.color = ApplyTransparentFog(output.color, input.position);
	return output;
}
