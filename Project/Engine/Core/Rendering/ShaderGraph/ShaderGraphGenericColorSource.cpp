#include "ShaderGraphGenericSource.h"

//============================================================================
//	include
//============================================================================

std::string Engine::ShaderGraphStageSource::BuildGenericColorFunctions() {

	return R"hlsl(
float3 ShaderGraphSRGBToLinear(float3 color) {
	float3 low = color / 12.92;
	float3 high = pow(max((color + 0.055) / 1.055, 0), 2.4);
	return lerp(low, high, step(0.04045, color));
}

float3 ShaderGraphLinearToSRGB(float3 color) {
	float3 low = color * 12.92;
	float3 high = 1.055 * pow(max(color, 0), 1.0 / 2.4) - 0.055;
	return lerp(low, high, step(0.0031308, color));
}

float3 ShaderGraphRGBToHSV(float3 color) {
	float maximum = max(color.r, max(color.g, color.b));
	float minimum = min(color.r, min(color.g, color.b));
	float chroma = maximum - minimum;
	if (chroma < 1e-10) { return float3(0, 0, maximum); }
	float sector = maximum == color.r ? (color.g - color.b) / chroma :
		(maximum == color.g ? (color.b - color.r) / chroma + 2.0 : (color.r - color.g) / chroma + 4.0);
	return float3(frac(sector / 6.0), maximum == 0 ? 0 : chroma / maximum, maximum);
}

float3 ShaderGraphHSVToRGB(float3 color) {
	float sector = frac(color.x) * 6.0;
	float chroma = color.z * color.y;
	float rising = frac(sector) * chroma;
	float falling = chroma - rising;
	float minimum = color.z - chroma;
	switch ((uint)floor(sector)) {
	case 0u: return float3(chroma, rising, 0) + minimum;
	case 1u: return float3(falling, chroma, 0) + minimum;
	case 2u: return float3(0, chroma, rising) + minimum;
	case 3u: return float3(0, falling, chroma) + minimum;
	case 4u: return float3(rising, 0, chroma) + minimum;
	default: return float3(chroma, 0, falling) + minimum;
	}
}

float3 ShaderGraphHue(float3 color, float offset) {
	float3 hsv = ShaderGraphRGBToHSV(color);
	hsv.x = frac(hsv.x + offset);
	return ShaderGraphHSVToRGB(hsv);
}

float3 ShaderGraphWhiteBalance(float3 color, float temperature, float tint) {
	float3x3 rgbToXYZ = float3x3(0.4124564,0.3575761,0.1804375,
		0.2126729,0.7151522,0.0721750, 0.0193339,0.1191920,0.9503041);
	float3x3 xyzToRGB = float3x3(3.2404542,-1.5371385,-0.4985314,
		-0.9692660,1.8760108,0.0415560, 0.0556434,-0.2040259,1.0572252);
	float3x3 bradford = float3x3(0.8951,0.2664,-0.1614,
		-0.7502,1.7135,0.0367, 0.0389,-0.0685,1.0296);
	float3x3 inverseBradford = float3x3(0.9869929,-0.1470543,0.1599627,
		0.4323053,0.5183603,0.0492912, -0.0085287,0.0400428,0.9684867);
	float2 xy = clamp(float2(0.3127266,0.3290231) + float2(-temperature, tint) * 0.04, 0.01, 0.95);
	float3 sourceWhite = float3(xy.x, xy.y, 1.0-xy.x-xy.y) / xy.y;
	float3 destinationWhite = float3(0.95047,1.0,1.08883);
	float3 ratio = mul(bradford, destinationWhite) / max(mul(bradford, sourceWhite), 1e-5);
	return mul(xyzToRGB, mul(inverseBradford, mul(bradford, mul(rgbToXYZ, color)) * ratio));
}

float3 ShaderGraphNormalBlendReoriented(float3 a, float3 b) {
	float3 unit = normalize(a);
	if (unit.z < -0.999999) { return float3(b.x, -b.y, -b.z); }
	float4 rotation = normalize(float4(-unit.y, unit.x, 0, 1.0 + unit.z));
	return b + 2.0 * cross(rotation.xyz, cross(rotation.xyz, b) + rotation.w * b);
}

float3 ShaderGraphNormalFromHeight(float height, float strength, float3 position, float3 normal, float3x3 tangentToWorld) {
	float3 dx = ddx(position) - normal * dot(normal, ddx(position));
	float3 dy = ddy(position) - normal * dot(normal, ddy(position));
	float xx = dot(dx, dx), yy = dot(dy, dy), xy = dot(dx, dy);
	float determinant = max(xx * yy - xy * xy, 1e-20);
	float dhx = ddx(height), dhy = ddy(height);
	float3 gradient = (dx * (yy * dhx - xy * dhy) + dy * (xx * dhy - xy * dhx)) / determinant;
	float3 result = normalize(normal - strength * gradient);
	return mul(result, transpose(tangentToWorld));
}

ShaderGraphGradient ShaderGraphDefaultGradient() {
	ShaderGraphGradient gradient = (ShaderGraphGradient)0;
	gradient.colorCount = 2;
	gradient.alphaCount = 2;
	gradient.colors[0] = float4(0, 0, 0, 0);
	gradient.colors[1] = float4(1, 1, 1, 1);
	gradient.alphas[0] = float2(1, 0);
	gradient.alphas[1] = float2(1, 1);
	return gradient;
}

float4 ShaderGraphSampleGradient(ShaderGraphGradient gradient, float time) {
	float3 color = gradient.colors[0].rgb;
	float alpha = gradient.alphas[0].x;
	[loop] for (uint i = 1; i < min(gradient.colorCount, 8u); ++i) {
		float delta = gradient.colors[i].w - gradient.colors[i-1].w;
		float blend = delta < 1e-6 ? step(gradient.colors[i].w, time) :
			saturate((time - gradient.colors[i-1].w) / delta);
		if (gradient.fixedMode != 0u) { blend = step(0.01, blend); }
		color = lerp(color, gradient.colors[i].rgb, blend);
	}
	[loop] for (uint i = 1; i < min(gradient.alphaCount, 8u); ++i) {
		float delta = gradient.alphas[i].y - gradient.alphas[i-1].y;
		float blend = delta < 1e-6 ? step(gradient.alphas[i].y, time) :
			saturate((time - gradient.alphas[i-1].y) / delta);
		if (gradient.fixedMode != 0u) { blend = step(0.01, blend); }
		alpha = lerp(alpha, gradient.alphas[i].x, blend);
	}
	return float4(ShaderGraphSRGBToLinear(color), alpha);
}

float4 ShaderGraphBlend(float4 baseColor, float4 blendColor, float opacity, uint mode) {
	float4 result = blendColor;
	switch (mode) {
	case 0u: result = 1.0 - (1.0 - blendColor) / baseColor; break;
	case 1u: result = min(baseColor, blendColor); break;
	case 2u: result = abs(baseColor - blendColor); break;
	case 3u: result = baseColor / (1.0 - blendColor); break;
	case 4u: result = baseColor / (blendColor + 1e-12); break;
	case 5u: result = blendColor + baseColor - 2.0 * blendColor * baseColor; break;
	case 6u: result = lerp(2.0 * baseColor * blendColor, 1.0 - 2.0 * (1.0-baseColor) * (1.0-blendColor), step(0.5, blendColor)); break;
	case 7u: result = step(1.0 - baseColor, blendColor); break;
	case 8u: result = max(baseColor, blendColor); break;
	case 9u: result = baseColor + blendColor - 1.0; break;
	case 10u: result = baseColor + blendColor; break;
	case 11u: result = lerp(max(baseColor + 2.0*blendColor - 1.0, 0), min(baseColor + 2.0*(blendColor-0.5), 1), step(0.5, blendColor)); break;
	case 12u: result = blendColor + 2.0 * baseColor - 1.0; break;
	case 13u: result = baseColor * blendColor; break;
	case 14u: result = 1.0 - abs(1.0 - blendColor - baseColor); break;
	case 15u: result = lerp(2.0 * baseColor * blendColor, 1.0 - 2.0*(1.0-baseColor)*(1.0-blendColor), step(0.5, baseColor)); break;
	case 16u: result = lerp(min(2.0 * baseColor, blendColor), max(2.0*(baseColor-0.5), blendColor), step(0.5, blendColor)); break;
	case 17u: result = 1.0 - (1.0-baseColor) * (1.0-blendColor); break;
	case 18u: result = lerp(2.0*baseColor*blendColor + baseColor*baseColor*(1.0-2.0*blendColor),
		sqrt(max(baseColor, 0))*(2.0*blendColor-1.0) + 2.0*baseColor*(1.0-blendColor), step(0.5, blendColor)); break;
	case 19u: result = baseColor - blendColor; break;
	case 20u: result = lerp(1.0-(1.0-blendColor)/(2.0*baseColor), blendColor/(2.0*(1.0-baseColor)), step(0.5, baseColor)); break;
	default: break;
	}
	return lerp(baseColor, result, opacity);
}
)hlsl";
}
