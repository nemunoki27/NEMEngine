#ifndef NEM_SHINE_HIGHLIGHT_HLSLI
#define NEM_SHINE_HIGHLIGHT_HLSLI

#include "descriptorHeapCompatibility.hlsli"

struct ShineHighlightData {

	float4 color;
	float2 direction;
	float position;
	float width;
	float softness;
	float intensity;
	uint rampIndex;
	float padding;
};

float4 ApplyShineHighlight(float4 color, float2 localUV, ShineHighlightData shine) {

	if (shine.intensity <= 0.0f || color.a <= 0.0f) return color;

	float span = dot(abs(shine.direction), 1.0f.xx);
	float projection = dot(localUV - 0.5f, shine.direction) / max(span, 0.00001f);
	float center = (shine.position - 0.5f) * (1.0f + shine.width);
	float t = (projection - center) / max(shine.width, 0.00001f) + 0.5f;
	if (t <= 0.0f || 1.0f <= t) return color;

	float mask = smoothstep(0.0f, max(shine.softness * 0.5f, 0.00001f), min(t, 1.0f - t));
	float4 tint = shine.color;
	if (shine.rampIndex != 0xffffffffu) {

		Texture2D<float4> ramp = NEM_TEXTURE2D(shine.rampIndex);
		uint width, height;
		ramp.GetDimensions(width, height);
		float x = t * (width - 1u);
		uint2 pixel = uint2(x, height / 2u);
		tint *= lerp(ramp.Load(int3(pixel, 0)), ramp.Load(int3(min(pixel.x + 1u, width - 1u), pixel.y, 0)), frac(x));
	}
	color.rgb += tint.rgb * (tint.a * shine.intensity * mask);
	return color;
}

#endif // NEM_SHINE_HIGHLIGHT_HLSLI
