#include "bloomCommon.hlsli"

RWTexture2D<float4> gDestColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (any(pixel.xy >= uint2(resolution))) return;
	uint width, height;
	gSourceColor.GetDimensions(width, height);
	float2 texel = 1.0f / float2(width, height);
	float2 uv = (float2(pixel.xy) + 0.5f) * invResolution;
	// 13点の重みを合計1にして縮小する
	float3 color = BloomSource(uv) * 0.125f;
	color += (BloomSource(uv + texel * float2(-2, -2)) + BloomSource(uv + texel * float2(2, -2)) +
		BloomSource(uv + texel * float2(-2, 2)) + BloomSource(uv + texel * float2(2, 2))) * 0.03125f;
	color += (BloomSource(uv + texel * float2(-2, 0)) + BloomSource(uv + texel * float2(2, 0)) +
		BloomSource(uv + texel * float2(0, -2)) + BloomSource(uv + texel * float2(0, 2))) * 0.0625f;
	color += (BloomSource(uv + texel * float2(-1, -1)) + BloomSource(uv + texel * float2(1, -1)) +
		BloomSource(uv + texel * float2(-1, 1)) + BloomSource(uv + texel * float2(1, 1))) * 0.125f;
	gDestColor[pixel.xy] = float4(BloomColor(color), 1.0f);
}
