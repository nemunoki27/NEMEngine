#include "bloomCommon.hlsli"

Texture2D<float4> gBloomColor : register(t1);
RWTexture2D<float4> gDestColor : register(u0);

// 小さい画像の9点を滑らかにつなぐ
float3 SampleBloom(float2 uv) {

	uint width, height;
	gBloomColor.GetDimensions(width, height);
	float2 texel = 1.0f / float2(width, height);
	float3 color = gBloomColor.SampleLevel(gBloomSampler, uv, 0).rgb * 4.0f;
	color += (gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(-1, 0), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(1, 0), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(0, -1), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(0, 1), 0).rgb) * 2.0f;
	color += gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(-1, -1), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(1, -1), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(-1, 1), 0).rgb +
		gBloomColor.SampleLevel(gBloomSampler, uv + texel * float2(1, 1), 0).rgb;
	return BloomColor(color * (1.0f / 16.0f));
}

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (any(pixel.xy >= uint2(resolution))) return;
	float2 uv = (float2(pixel.xy) + 0.5f) * invResolution;
	float4 source = gSourceColor.SampleLevel(gBloomSampler, uv, 0);
	float3 blurred = SampleBloom(uv);
#ifdef NEM_BLOOM_COMPOSITE
	// 元画像の色と透明度を保持して一度だけ加算する
	gDestColor[pixel.xy] = float4(source.rgb + blurred * intensity * tint.rgb, source.a);
#else
	// 広がりだけを変え、段数による強度の増加を防ぐ
	gDestColor[pixel.xy] = float4(lerp(BloomColor(source.rgb), blurred, scatter), 1.0f);
#endif
}
