#include "../Common/postProcessCommon.hlsli"

cbuffer PostProcessParameters : register(b1) {

	float strength;
	float edgeThreshold;
	float minimumThreshold;
	float subpixelStrength;
	float4 viewRect;
};

float4 SampleFXAA(float2 uv) {

	float2 minimum = viewRect.z > 0.0f ? (viewRect.xy + 0.5f) * invResolution : invResolution * 0.5f;
	float2 maximum = viewRect.z > 0.0f ? (viewRect.xy + viewRect.zw - 0.5f) * invResolution : 1.0f.xx - invResolution * 0.5f;
	return SampleSource(clamp(uv, minimum, maximum));
}

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (viewRect.z > 0.0f && viewRect.w > 0.0f) {
		if (any(pixel.xy >= uint2(viewRect.zw))) return;
		pixel.xy += uint2(viewRect.xy);
	}
	if (IsOutside(pixel.xy)) return;
	float2 uv = (float2(pixel.xy) + 0.5f) * invResolution;
	float4 center = SampleFXAA(uv);
	float3 northwest = SampleFXAA(uv - invResolution).rgb;
	float3 northeast = SampleFXAA(uv + float2(1, -1) * invResolution).rgb;
	float3 southwest = SampleFXAA(uv + float2(-1, 1) * invResolution).rgb;
	float3 southeast = SampleFXAA(uv + invResolution).rgb;
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
