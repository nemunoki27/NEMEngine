#ifndef NEM_INLINE_SHADOW_COMMON_HLSLI
#define NEM_INLINE_SHADOW_COMMON_HLSLI

RaytracingAccelerationStructure gSceneTLAS : register(t10);

// 影を落とすインスタンスのTLASマスク
static const uint kRaytracingMaskShadowCaster = 1u;
static const uint kMaximumSoftShadowSampleCount = 4u;
static const float2 kSoftShadowDisk[kMaximumSoftShadowSampleCount] = {
	float2(0.353553f, 0.000000f),
	float2(-0.451180f, 0.414030f),
	float2(0.068910f, -0.787560f),
	float2(0.569130f, 0.742260f)
};
static const float2 kRectLightSamples[kMaximumSoftShadowSampleCount] = {
	float2(-0.375f, -0.125f),
	float2(0.125f, -0.375f),
	float2(-0.125f, 0.375f),
	float2(0.375f, 0.125f)
};

uint HashShadowSeed(uint value) {

	value ^= 2747636419u;
	value *= 2654435769u;
	value ^= value >> 16u;
	value *= 2654435769u;
	value ^= value >> 16u;
	value *= 2654435769u;
	return value;
}

float ResolveShadowRotation(uint2 pixel, uint lightIndex) {

	uint seed = pixel.x * 1973u + pixel.y * 9277u +
		lightIndex * 26699u;
	return float(HashShadowSeed(seed)) *
		(6.28318530718f / 4294967295.0f);
}

float2 RotateShadowDisk(float2 samplePos, float sinRotation, float cosRotation) {

	return float2(
		samplePos.x * cosRotation - samplePos.y * sinRotation,
		samplePos.x * sinRotation + samplePos.y * cosRotation);
}

uint ResolveShadowSampleIndex(uint sampleIndex, uint sampleCount,
	uint2 pixel, uint lightIndex) {

	uint offset = HashShadowSeed(pixel.x * 1973u + pixel.y * 9277u +
		lightIndex * 26699u) & 3u;
	uint step = kMaximumSoftShadowSampleCount / sampleCount;
	return (offset + sampleIndex * step) & 3u;
}

void BuildShadowBasis(float3 direction, out float3 tangent, out float3 bitangent) {

	float3 up = abs(direction.y) < 0.999f ?
		float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
	tangent = normalize(cross(up, direction));
	bitangent = cross(direction, tangent);
}

// 最初の遮蔽物で走査を終了する共通シャドウレイ
bool TraceShadowRay(float3 origin, float3 direction, float maxDistance, out float hitDistance) {

	RayDesc rayDesc;
	rayDesc.Origin = origin;
	rayDesc.Direction = direction;
	rayDesc.TMin = 0.001f;
	rayDesc.TMax = maxDistance;

	RayQuery <
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > rayQuery;

	// CastShadow無効のインスタンスはマスクで除外される
	rayQuery.TraceRayInline(gSceneTLAS, 0, kRaytracingMaskShadowCaster, rayDesc);
	while (rayQuery.Proceed()) {
	}
	bool hit = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	hitDistance = hit ? rayQuery.CommittedRayT() : 0.0f;
	return hit;
}

bool TraceShadowRay(float3 origin, float3 direction, float maxDistance) {

	float hitDistance;
	return TraceShadowRay(origin, direction, maxDistance, hitDistance);
}

// 平行光源の見かけの角度内へ分散したレイから遮蔽率を返す
float TraceDirectionalShadow(float3 worldPos, float3 worldNormal,
	float3 lightDirection, float angularRadius, uint2 pixel, uint lightIndex) {

	float3 origin = worldPos + worldNormal * shadowNormalBias;
	float3 centerDirection = normalize(-lightDirection);
	if (angularRadius <= 0.0001f) {
		return TraceShadowRay(origin, centerDirection, shadowMaxDistance) ?
			1.0f : 0.0f;
	}

	float3 tangent;
	float3 bitangent;
	BuildShadowBasis(centerDirection, tangent, bitangent);

	float rotation = ResolveShadowRotation(pixel, lightIndex);
	float sinRotation;
	float cosRotation;
	sincos(rotation, sinRotation, cosRotation);

	float coneRadius = tan(radians(min(angularRadius, 5.0f)));
	uint sampleCount = clamp(softShadowSampleCount,
		1u, kMaximumSoftShadowSampleCount);
	if (sampleCount == 1u) {
		return TraceShadowRay(origin, centerDirection,
			shadowMaxDistance) ? 1.0f : 0.0f;
	}
	float occlusion = 0.0f;
	[loop]
	for (uint sampleIndex = 0u;
		sampleIndex < sampleCount; ++sampleIndex) {

		uint diskIndex = ResolveShadowSampleIndex(
			sampleIndex, sampleCount, pixel, lightIndex);
		float2 disk = RotateShadowDisk(
			kSoftShadowDisk[diskIndex], sinRotation, cosRotation);
		float3 direction = normalize(centerDirection +
			(tangent * disk.x + bitangent * disk.y) * coneRadius);
		occlusion += TraceShadowRay(
			origin, direction, shadowMaxDistance) ? 1.0f : 0.0f;
	}
	return occlusion / float(sampleCount);
}

//============================================================================
//	点光源影
//============================================================================

// ローカルライトを円形面光源として遮蔽率を返す
float TraceLocalSoftShadow(float3 worldPos, float3 worldNormal,
	float3 toLight, float distToLight, float sourceRadius,
	uint2 pixel, uint lightIndex) {

	float3 origin = worldPos + worldNormal * shadowNormalBias;
	float3 centerDirection = toLight / distToLight;
	if (sourceRadius <= 0.0001f) {
		return TraceShadowRay(origin, centerDirection, distToLight) ?
			1.0f : 0.0f;
	}

	float3 tangent;
	float3 bitangent;
	BuildShadowBasis(centerDirection, tangent, bitangent);

	float rotation = ResolveShadowRotation(pixel, lightIndex);
	float sinRotation;
	float cosRotation;
	sincos(rotation, sinRotation, cosRotation);

	uint sampleCount = clamp(softShadowSampleCount,
		1u, kMaximumSoftShadowSampleCount);
	if (sampleCount == 1u) {
		return TraceShadowRay(origin, centerDirection,
			distToLight) ? 1.0f : 0.0f;
	}
	float occlusion = 0.0f;
	[loop]
	for (uint sampleIndex = 0u;
		sampleIndex < sampleCount; ++sampleIndex) {

		uint diskIndex = ResolveShadowSampleIndex(
			sampleIndex, sampleCount, pixel, lightIndex);
		float2 disk = RotateShadowDisk(
			kSoftShadowDisk[diskIndex], sinRotation, cosRotation);
		float3 sampleToLight = toLight +
			(tangent * disk.x + bitangent * disk.y) * sourceRadius;
		float sampleDistance = length(sampleToLight);
		occlusion += TraceShadowRay(origin,
			sampleToLight / sampleDistance, sampleDistance) ? 1.0f : 0.0f;
	}
	return occlusion / float(sampleCount);
}

float3 GetRectLightSamplePosition(RectLight light,
	float2 sampleUV) {

	return light.pos +
		light.right * (sampleUV.x * max(light.sourceWidth, 0.0f)) +
		light.up * (sampleUV.y * max(light.sourceHeight, 0.0f));
}

// 矩形面上の複数点へレイを飛ばして面積に応じた遮蔽率を返す
float TraceRectShadow(float3 worldPos, float3 worldNormal,
	RectLight light, uint2 pixel, uint lightIndex) {

	float3 origin = worldPos + worldNormal * shadowNormalBias;
	if (light.sourceWidth <= 0.0001f &&
		light.sourceHeight <= 0.0001f) {

		float3 toLight = light.pos - origin;
		float distanceToLight = length(toLight);
		if (distanceToLight <= 1e-5f) {
			return 0.0f;
		}
		return TraceShadowRay(origin,
			toLight / distanceToLight,
			distanceToLight) ? 1.0f : 0.0f;
	}

	uint seed = HashShadowSeed(
		pixel.x * 1973u + pixel.y * 9277u +
		lightIndex * 26699u);
	float2 sampleSign = float2(
		(seed & 1u) != 0u ? -1.0f : 1.0f,
		(seed & 2u) != 0u ? -1.0f : 1.0f);

	uint sampleCount = clamp(softShadowSampleCount,
		1u, kMaximumSoftShadowSampleCount);
	if (sampleCount == 1u) {
		float3 toLight = light.pos - origin;
		float distanceToLight = length(toLight);
		return distanceToLight > 1e-5f &&
			TraceShadowRay(origin, toLight / distanceToLight,
				distanceToLight) ? 1.0f : 0.0f;
	}
	float occlusion = 0.0f;
	[loop]
	for (uint sampleIndex = 0u;
		sampleIndex < sampleCount; ++sampleIndex) {

		uint rectSampleIndex = ResolveShadowSampleIndex(
			sampleIndex, sampleCount, pixel, lightIndex);
		float2 sampleUV =
			kRectLightSamples[rectSampleIndex] * sampleSign;
		float3 samplePos =
			GetRectLightSamplePosition(light, sampleUV);
		float3 toLight = samplePos - origin;
		float distanceToLight = length(toLight);
		if (distanceToLight <= 1e-5f) {
			continue;
		}
		occlusion += TraceShadowRay(origin,
			toLight / distanceToLight,
			distanceToLight) ? 1.0f : 0.0f;
	}
	return occlusion / float(sampleCount);
}


#endif
