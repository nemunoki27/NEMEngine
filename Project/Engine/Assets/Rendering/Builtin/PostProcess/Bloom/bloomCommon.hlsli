#ifndef NEM_BLOOM_COMMON_HLSLI
#define NEM_BLOOM_COMMON_HLSLI

//============================================================================
//	CBuffer
//============================================================================
cbuffer PostProcessFrameConstants : register(b0) {

	float2 resolution;
	float2 invResolution;
};
cbuffer PostProcessParameters : register(b1) {

	float threshold;
	float softKnee;
	float intensity;
	float scatter;
	float4 tint;
};
Texture2D<float4> gSourceColor : register(t0);
SamplerState gBloomSampler : register(s0);

// 非数値と半精度の範囲外を中間画像へ持ち込まない
float3 BloomColor(float3 color) {

	return float3(isfinite(color.r) ? clamp(color.r, 0.0f, 65504.0f) : 0.0f,
		isfinite(color.g) ? clamp(color.g, 0.0f, 65504.0f) : 0.0f,
		isfinite(color.b) ? clamp(color.b, 0.0f, 65504.0f) : 0.0f);
}

float3 BloomSource(float2 uv) {

	float3 color = BloomColor(gSourceColor.SampleLevel(gBloomSampler, uv, 0).rgb);
#ifdef NEM_BLOOM_PREFILTER
	// 閾値の前後をつなぎ、色の比率を保って抽出する
	float brightness = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
	float knee = threshold * softKnee;
	float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
	soft = soft * soft / max(4.0f * knee, 1.0e-6f);
	color *= saturate(max(brightness - threshold, soft) / max(brightness, 1.0e-6f));
#endif
	return color;
}
#endif
