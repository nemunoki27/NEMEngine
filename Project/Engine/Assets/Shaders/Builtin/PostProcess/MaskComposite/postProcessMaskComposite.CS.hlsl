//============================================================================
//	resources
//============================================================================
cbuffer PostProcessParameters : register(b1) {

	uint selectionMode;
	uint compositeMode;
	uint renderingLayerMask;
	float _padding;
};

Texture2D<float4> gSourceColor : register(t0);
Texture2D<float4> gEffectColor : register(t1);
Texture2D<float4> gSelectionMask : register(t2);
Texture2D<uint> gSourceFlags : register(t3);
RWTexture2D<float4> gDestColor : register(u0);
SamplerState gColorSampler : register(s0);

//============================================================================
//	main
//============================================================================
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {

	uint2 size;
	gDestColor.GetDimensions(size.x, size.y);
	if (any(dispatchThreadID.xy >= size)) {
		return;
	}

	// 出力解像度に合わせて入力カラーを取得
	const float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / float2(size);
	const float4 source = gSourceColor.SampleLevel(gColorSampler, uv, 0);
	const float4 effect = gEffectColor.SampleLevel(gColorSampler, uv, 0);
	uint2 flagsSize;
	gSourceFlags.GetDimensions(flagsSize.x, flagsSize.y);
	const uint2 flagsPixel = min(uint2(uv * float2(flagsSize)), flagsSize - 1u);
	const uint flags = gSourceFlags[flagsPixel];
	const uint opaqueLayerMask = (flags >> 8u) & 0x00FFFFFFu;
	const float opaqueCoverage = selectionMode == 1u &&
		(opaqueLayerMask & renderingLayerMask) != 0u ? 1.0f : 0.0f;
	const float coverage = max(opaqueCoverage,
		saturate(gSelectionMask.SampleLevel(gColorSampler, uv, 0).a));
	if (coverage <= 0.0f) {
		gDestColor[dispatchThreadID.xy] = source;
		return;
	}

	// Autoはシーンカラー方式なら置換、分離方式ならα合成を使う
	uint resolvedMode = compositeMode;
	if (resolvedMode == 0u) {
		resolvedMode = selectionMode == 1u ? 4u : 1u;
	}
	float4 result = effect;
	if (resolvedMode == 1u) {
		result = float4(
			effect.rgb + source.rgb * (1.0f - effect.a),
			effect.a + source.a * (1.0f - effect.a));
	} else if (resolvedMode == 2u) {
		result = float4(source.rgb + effect.rgb, max(source.a, effect.a));
	} else if (resolvedMode == 3u) {
		result = float4(1.0f - (1.0f - source.rgb) *
			(1.0f - effect.rgb), max(source.a, effect.a));
	}
	gDestColor[dispatchThreadID.xy] = lerp(source, result, coverage);
}
