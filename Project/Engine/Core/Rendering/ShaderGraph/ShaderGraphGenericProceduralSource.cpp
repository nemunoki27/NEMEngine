#include "ShaderGraphGenericSource.h"

//============================================================================
//	include
//============================================================================

std::string Engine::ShaderGraphStageSource::BuildGenericProceduralFunctions() {

	return R"hlsl(
float2 ShaderGraphGradientNoiseDirection(float2 p) {
	uint2 cell = asuint(int2(p));
	uint state = cell.x * 1664525u + cell.y * 1013904223u;
	state ^= state >> 13u;
	state *= 2246822519u;
	state ^= state >> 16u;
	float angle = float(state & 0x00FFFFFFu) * (6.283185307 / 16777216.0);
	return float2(cos(angle), sin(angle));
}

float ShaderGraphGradientNoise(float2 uv) {
	float2 cell = floor(uv);
	float2 local = frac(uv);
	float a = dot(ShaderGraphGradientNoiseDirection(cell), local);
	float b = dot(ShaderGraphGradientNoiseDirection(cell + float2(1,0)), local - float2(1,0));
	float c = dot(ShaderGraphGradientNoiseDirection(cell + float2(0,1)), local - float2(0,1));
	float d = dot(ShaderGraphGradientNoiseDirection(cell + float2(1,1)), local - float2(1,1));
	float2 blend = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);
	return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y) + 0.5;
}

float2 ShaderGraphCheckerIntegral(float2 coordinate) {
	float2 phase = frac(coordinate * 0.5) * 2.0;
	return 1.0 - abs(phase - 1.0);
}

float3 ShaderGraphCheckerboard(float2 uv, float3 a, float3 b, float2 frequency) {
	float2 coordinate = uv * frequency;
	float2 footprint = max(abs(ddx(coordinate)) + abs(ddy(coordinate)), 1e-4);
	float2 high = ShaderGraphCheckerIntegral(coordinate + footprint * 0.5);
	float2 low = ShaderGraphCheckerIntegral(coordinate - footprint * 0.5);
	float2 average = clamp((high - low) / footprint, -1.0, 1.0);
	return lerp(a, b, 0.5 - 0.5 * average.x * average.y);
}

float ShaderGraphEllipse(float2 uv, float width, float height) {
	float distance = length((uv * 2.0 - 1.0) / max(abs(float2(width, height)), 1e-5));
	return saturate((1.0 - distance) / max(fwidth(distance), 1e-5));
}

float ShaderGraphRectangle(float2 uv, float width, float height) {
	float2 distance = abs(uv * 2.0 - 1.0) - abs(float2(width, height));
	float2 coverage = saturate(1.0 - distance / max(fwidth(distance), 1e-5));
	return min(coverage.x, coverage.y);
}

float ShaderGraphRoundedRectangle(float2 uv, float width, float height, float radius) {
	float2 halfSize = abs(float2(width, height));
	float corner = clamp(abs(radius * 2.0), 0.0, min(halfSize.x, halfSize.y));
	float2 q = abs(uv * 2.0 - 1.0) - halfSize + corner;
	float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - corner;
	return saturate(0.5 - distance / max(fwidth(distance), 1e-5));
}

float ShaderGraphRoundedPolygon(float2 uv, float width, float height, float sides, float roundness) {
	float2 coordinate = (uv * 2.0 - 1.0) / max(abs(float2(width, height)), 1e-5);
	uint count = (uint)clamp(floor(sides + 0.5), 3.0, 64.0);
	float radius = saturate(roundness) * 0.5;
	float distance = 1e5;
	bool inside = true;
	[loop] for (uint i = 0u; i < count; ++i) {
		float angleA = 6.283185307 * float(i) / float(count) + 1.570796327;
		float angleB = 6.283185307 * float(i + 1u) / float(count) + 1.570796327;
		float2 a = float2(cos(angleA), sin(angleA)) * (1.0 - radius);
		float2 b = float2(cos(angleB), sin(angleB)) * (1.0 - radius);
		float2 edge = b - a;
		float2 delta = coordinate - a;
		float edgeDistance = length(delta - edge * saturate(dot(delta, edge) / max(dot(edge, edge), 1e-8)));
		distance = min(distance, edgeDistance);
		inside = inside && 0.0 <= edge.x * delta.y - edge.y * delta.x;
	}
	distance = (inside ? -distance : distance) - radius;
	return saturate(0.5 - distance / max(fwidth(distance), 1e-5));
}
)hlsl";
}
