#include "../Common/postProcessCommon.hlsli"

cbuffer PostProcessParameters : register(b1) {

	float strength;
	float edgeThreshold;
	float minimumThreshold;
	float subpixelStrength;
	float4 viewRect;
};

groupshared float4 sharedSource[144];

float2 ClampFXAAUV(float2 uv) {

	float2 minimum = viewRect.z > 0.0f ? (viewRect.xy + 0.5f) * invResolution : invResolution * 0.5f;
	float2 maximum = viewRect.z > 0.0f ? (viewRect.xy + viewRect.zw - 0.5f) * invResolution : 1.0f.xx - invResolution * 0.5f;
	return clamp(uv, minimum, maximum);
}

float4 SampleFXAA(float2 uv) {

	return SampleSource(ClampFXAAUV(uv));
}

uint GetFXAATileIndex(int2 pixel, int2 origin) {

	int2 offset = pixel - origin + 2;
	return uint(offset.y * 12 + offset.x);
}

float4 SampleFXAAInitial(float2 uv, int2 origin, bool useShared) {

	if (!useShared) return SampleFXAA(uv);
	// 元の双線形補間を保ち、同じ解像度の近傍参照だけ共有する
	float2 position = saturate(ClampFXAAUV(uv)) * resolution - 0.5f;
	int2 base = int2(floor(position));
	float2 weight = frac(position);
	int2 maximum = int2(resolution) - 1;
	float4 top = lerp(sharedSource[GetFXAATileIndex(clamp(base, 0, maximum), origin)],
		sharedSource[GetFXAATileIndex(clamp(base + int2(1, 0), 0, maximum), origin)], weight.x);
	float4 bottom = lerp(sharedSource[GetFXAATileIndex(clamp(base + int2(0, 1), 0, maximum), origin)],
		sharedSource[GetFXAATileIndex(clamp(base + 1, 0, maximum), origin)], weight.x);
	return lerp(top, bottom, weight.y);
}

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThread : SV_GroupThreadID) {

	uint2 outputSize = viewRect.z > 0.0f && viewRect.w > 0.0f ? uint2(viewRect.zw) : uint2(resolution);
	if (any(groupID.xy * 8u >= outputSize)) return;
	uint sourceWidth, sourceHeight;
	gSourceColor.GetDimensions(sourceWidth, sourceHeight);
	bool useShared = all(uint2(sourceWidth, sourceHeight) == uint2(resolution));
	int2 origin = int2(groupID.xy * 8u);
	if (viewRect.z > 0.0f && viewRect.w > 0.0f) origin += int2(viewRect.xy);
	if (useShared) {

		for (uint index = groupThread.y * 8u + groupThread.x; index < 144u; index += 64u) {

			int2 sourcePixel = clamp(origin + int2(index % 12u, index / 12u) - 2, int2(0, 0), int2(resolution) - 1);
			sharedSource[index] = gSourceColor.Load(int3(sourcePixel, 0));
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (viewRect.z > 0.0f && viewRect.w > 0.0f) {
		if (any(pixel.xy >= uint2(viewRect.zw))) return;
		pixel.xy += uint2(viewRect.xy);
	}
	if (IsOutside(pixel.xy)) return;
	float2 uv = (float2(pixel.xy) + 0.5f) * invResolution;
	float4 center = SampleFXAAInitial(uv, origin, useShared);
	float3 northwest = SampleFXAAInitial(uv - invResolution, origin, useShared).rgb;
	float3 northeast = SampleFXAAInitial(uv + float2(1, -1) * invResolution, origin, useShared).rgb;
	float3 southwest = SampleFXAAInitial(uv + float2(-1, 1) * invResolution, origin, useShared).rgb;
	float3 southeast = SampleFXAAInitial(uv + invResolution, origin, useShared).rgb;
	// HDRの強い発光が方向判定を支配しないよう圧縮する
	float lumaCenter = SourceLuminance(center.rgb);
	float4 luma = float4(SourceLuminance(northwest), SourceLuminance(northeast),
		SourceLuminance(southwest), SourceLuminance(southeast));
	lumaCenter = lumaCenter / (1.0f + max(lumaCenter, 0.0f));
	luma = luma / (1.0f + max(luma, 0.0f));
	float minimum = min(lumaCenter, min(min(luma.x, luma.y), min(luma.z, luma.w)));
	float maximum = max(lumaCenter, max(max(luma.x, luma.y), max(luma.z, luma.w)));
	if (maximum - minimum < max(minimumThreshold, maximum * edgeThreshold)) {
		gDestColor[pixel.xy] = center;
		return;
	}
	float2 direction = float2(-(luma.x + luma.y - luma.z - luma.w), luma.x + luma.z - luma.y - luma.w);
	float reduction = max(dot(luma, 0.25f.xxxx) * 0.125f, 1.0f / 128.0f);
	direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + reduction), -8.0f, 8.0f) * invResolution;
	float3 narrow = 0.5f * (SampleFXAA(uv - direction / 6.0f).rgb + SampleFXAA(uv + direction / 6.0f).rgb);
	float3 wide = narrow * 0.5f + 0.25f * (SampleFXAA(uv - direction * 0.5f).rgb + SampleFXAA(uv + direction * 0.5f).rgb);
	float wideLuma = SourceLuminance(wide);
	wideLuma /= 1.0f + max(wideLuma, 0.0f);
	float3 filtered = wideLuma < minimum || wideLuma > maximum ? narrow : wide;
	float blend = saturate(strength) * saturate(subpixelStrength);
	gDestColor[pixel.xy] = float4(lerp(center.rgb, filtered, blend), center.a);
}
