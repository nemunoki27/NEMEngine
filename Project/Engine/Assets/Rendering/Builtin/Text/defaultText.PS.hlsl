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
	float3 msdf;
	if (enableOutline && outlineWidth > 0.0f) {
		float2 halfTexel = 0.5f / instance.atlasSize;
		float2 inset = min(halfTexel, 0.5f * (input.atlasBounds.zw - input.atlasBounds.xy));
		float2 uv = clamp(input.texcoord, input.atlasBounds.xy + inset, input.atlasBounds.zw - inset);
		msdf = gAtlas.SampleLevel(gSampler, uv, 0).rgb;
	} else {
		msdf = gAtlas.Sample(gSampler, input.texcoord).rgb;
	}
	float4 transformedUV = mul(float4(input.materialTexcoord, 0.0f, 1.0f), instance.uvMatrix);
	float4 fillColor = baseColorTexture.Sample(gSampler, transformedUV.xy) * color;
	float signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5f;
	float screenRange = ComputeScreenPxRange(input.texcoord, instance.pxRange, instance.atlasSize);
	float screenPxDistance = screenRange * signedDistance;

	// 文字本体のカバレッジ
	float fillAlpha = saturate(screenPxDistance + 0.5f);

	PSOutput output;

	// アウトライン有効時は本体より広い距離でカバレッジを取り、縁を別色で塗る
	if (enableOutline && outlineWidth > 0.0f) {

		float pixelScale = min(referenceToRenderScale.x, referenceToRenderScale.y);
		// 距離場の外端にアンチエイリアス用の余白を残す
		float limit = max(0.0f, 0.5f * screenRange - 0.5f);
		float width = outlineWidth * pixelScale;
		float transition = min(0.5f, limit * 0.5f);
		float overlap = max(transition - abs(width - limit), 0.0f);
		width = min(width, limit) - overlap * overlap / max(4.0f * transition, 1e-6f);
		float outerAlpha = saturate(screenPxDistance + width + 0.5f);
		float fill = fillAlpha * fillColor.a;
		float outline = (outerAlpha - fillAlpha) * outlineColor.a;
		float alpha = fill + outline;
		output.color = float4((fillColor.rgb * fill + outlineColor.rgb * outline) / max(alpha, 1e-6f), alpha);
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
