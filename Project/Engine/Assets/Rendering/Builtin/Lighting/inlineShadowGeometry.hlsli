#ifndef NEM_INLINE_SHADOW_GEOMETRY_HLSLI
#define NEM_INLINE_SHADOW_GEOMETRY_HLSLI

float3 GetShadowSurfaceDerivative(int2 pixel, int2 axis, float3 position) {

	int2 maximum = int2(shadowSize) - 1;
	float4 forward = gWorldPos.Load(int3(clamp(pixel + axis, int2(0, 0), maximum), 0));
	float4 backward = gWorldPos.Load(int3(clamp(pixel - axis, int2(0, 0), maximum), 0));
	float3 first = forward.xyz - position;
	float3 second = position - backward.xyz;
	bool firstValid = forward.w > 0.0f && dot(first, first) > 1e-12f;
	bool secondValid = backward.w > 0.0f && dot(second, second) > 1e-12f;
	if (!firstValid && !secondValid) return 0.0f.xxx;
	if (!firstValid) return second;
	if (!secondValid) return first;
	return dot(first, first) < dot(second, second) ? first : second;
}

// 法線Mapの凹凸を影の境界として扱わない
float3 GetShadowSurfaceNormal(int2 pixel, float3 position) {

	float3 shadingNormal = normalize(gNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
	float3 horizontal = GetShadowSurfaceDerivative(pixel, int2(1, 0), position);
	float3 vertical = GetShadowSurfaceDerivative(pixel, int2(0, 1), position);
	float3 normal = cross(horizontal, vertical);
	if (dot(normal, normal) < 1e-12f) return shadingNormal;
	normal = normalize(normal);
	return dot(normal, shadingNormal) < 0.0f ? -normal : normal;
}

#endif
