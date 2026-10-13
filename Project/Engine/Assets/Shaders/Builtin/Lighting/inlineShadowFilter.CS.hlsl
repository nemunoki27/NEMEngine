//============================================================================
//	include
//============================================================================
#include "inlineShadowParameters.hlsli"

Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gWorldPos : register(t2);
Texture2D<float2> gMotion : register(t3);
StructuredBuffer<float2> gShadowInput : register(t20);
StructuredBuffer<float2> gShadowRaw : register(t21);
StructuredBuffer<float2> gShadowHistory : register(t22);
Texture2D<float4> gPreviousPosition : register(t23);
Texture2D<float4> gPreviousNormal : register(t24);
Texture2D<float4> gSurfaceNormal : register(t25);

RWStructuredBuffer<float2> gShadowOutput : register(u0);
RWTexture2D<float4> gCurrentPosition : register(u1);
RWTexture2D<float4> gCurrentNormal : register(u2);
RWTexture2D<float> gShadowPreview : register(u3);

groupshared float4 sharedPosition[144];
groupshared float3 sharedNormal[144];
groupshared float2 sharedRaw[144];

float3 GetSharedSurfaceDerivative(uint index, int axis, float3 position) {

	float4 forward = sharedPosition[int(index) + axis];
	float4 backward = sharedPosition[int(index) - axis];
	float3 first = forward.xyz - position;
	float3 second = position - backward.xyz;
	bool firstValid = forward.w > 0.0f && dot(first, first) > 1e-12f;
	bool secondValid = backward.w > 0.0f && dot(second, second) > 1e-12f;
	if (!firstValid && !secondValid) return 0.0f.xxx;
	if (!firstValid) return second;
	if (!secondValid) return first;
	return dot(first, first) < dot(second, second) ? first : second;
}

float3 GetSharedSurfaceNormal(uint index, float3 position) {

	// 面の法線は共有済みの隣接位置から一度だけ求める
	float3 normal = cross(GetSharedSurfaceDerivative(index, 1, position), GetSharedSurfaceDerivative(index, 12, position));
	if (dot(normal, normal) < 1e-12f) return sharedNormal[index];
	normal = normalize(normal);
	return dot(normal, sharedNormal[index]) < 0.0f ? -normal : normal;
}

float SurfaceWeightFromGeometry(float3 center, float3 normal, float4 position, float3 sampleNormal, float pixelSize) {

	if (position.w <= 0.0f) return 0.0f;
	float planeDistance = abs(dot(position.xyz - center, normal));
	const float normalWeight = saturate(dot(normal, sampleNormal));
	return exp(-planeDistance / max(pixelSize * 0.5f, 0.002f)) * normalWeight * normalWeight;
}

float SurfaceWeight(float3 center, float3 normal, int2 pixel, float pixelSize) {

	return SurfaceWeightFromGeometry(center, normal, gWorldPos.Load(int3(pixel, 0)),
		normalize(gNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f), pixelSize);
}

[numthreads(8, 8, 1)]
void temporal(uint3 dispatchID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThread : SV_GroupThreadID) {

	// 周囲二画素の位置と標本をGroup内で共有する
	for (uint index = groupThread.y * 8u + groupThread.x; index < 144u; index += 64u) {

		int2 samplePixel = clamp(int2(groupID.xy * 8u) + int2(index % 12u, index / 12u) - 2,
			int2(0, 0), int2(shadowSize) - 1);
		sharedPosition[index] = gWorldPos.Load(int3(samplePixel, 0));
		sharedNormal[index] = normalize(gNormal.Load(int3(samplePixel, 0)).xyz * 2.0f - 1.0f);
		sharedRaw[index] = gShadowRaw[GetShadowOffset(samplePixel, dispatchID.z)];
	}
	GroupMemoryBarrierWithGroupSync();

	uint2 pixel = dispatchID.xy;
	uint slot = dispatchID.z;
	if (any(pixel >= shadowSize) || slot >= shadowLightCount) return;
	uint offset = GetShadowOffset(pixel, slot);
	uint centerIndex = (groupThread.y + 2u) * 12u + groupThread.x + 2u;
	float4 position = sharedPosition[centerIndex];
	float3 normal = GetSharedSurfaceNormal(centerIndex, position.xyz);
	if (slot == 0u) {
		gCurrentPosition[pixel] = position;
		gCurrentNormal[pixel] = float4(normal * 0.5f + 0.5f, position.w);
	}
	float2 raw = sharedRaw[centerIndex];
	if (position.w <= 0.0f || raw.y == 0.0f) {
		gShadowOutput[offset] = float2(raw.x, PackShadowHistory(0.0f, 1.0f)); return;
	}
	float pixelSize = GetShadowPixelSize(position.xyz);
	float sum = 0.0f;
	float squaredSum = 0.0f;
	float radiusSum = 0.0f;
	float weightSum = 0.0f;
	float minimum = 1.0f;
	float maximum = 0.0f;
	float footprint = pixelSize;

	// 同じ面の標本だけで現在の遮蔽率を安定させる
	[unroll]
	for (int y = -2; y <= 2; ++y) {
		[unroll]
		for (int x = -2; x <= 2; ++x) {

			int2 samplePixel = int2(pixel) + int2(x, y);
			if (any(samplePixel < 0) || any(samplePixel >= int2(shadowSize))) continue;
			uint sampleIndex = uint(int(centerIndex) + y * 12 + x);
			float weight = x == 0 && y == 0 ? 1.0f :
				SurfaceWeightFromGeometry(position.xyz, normal, sharedPosition[sampleIndex], sharedNormal[sampleIndex], pixelSize) *
				exp(-float(x * x + y * y) / 4.0f);
			if (weight < 0.01f) continue;
			float value = sharedRaw[sampleIndex].x;
			radiusSum += sharedRaw[sampleIndex].y * weight;
			sum += value * weight;
			squaredSum += value * value * weight;
			weightSum += weight;
			minimum = min(minimum, value);
			maximum = max(maximum, value);
			if (abs(x) + abs(y) == 1) {
				footprint = max(footprint, length(sharedPosition[sampleIndex].xyz - position.xyz));
			}
		}
	}
	float current = sum / max(weightSum, 0.0001f);
	float radius = radiusSum / max(weightSum, 0.0001f);
	float count = 1.0f;
	float2 uv = (float2(pixel) + 0.5f) / float2(shadowSize);
	float2 motion = gMotion.Load(int3(pixel, 0));
	float4 previousClip = mul(float4(position.xyz, 1.0f), shadowPreviousViewProjection);
	float2 previousUV = dot(motion, motion) > 1e-12f ? uv - motion :
		previousClip.xy / max(previousClip.w, 0.0001f) * float2(0.5f, -0.5f) + 0.5f;

	// 再投影先の位置と法線が一致する履歴だけを使う
	if (resetShadowHistory == 0u && previousClip.w > 0.0f && all(previousUV > 0.0f.xx) && all(previousUV < 1.0f.xx)) {

		int2 previousPixel = min(int2(previousUV * shadowSize), int2(shadowSize) - 1);
		float4 previousPosition = gPreviousPosition.Load(int3(previousPixel, 0));
		float3 previousNormal = normalize(gPreviousNormal.Load(int3(previousPixel, 0)).xyz * 2.0f - 1.0f);
		float3 delta = previousPosition.xyz - position.xyz;
		if (previousPosition.w > 0.0f && abs(dot(delta, normal)) < max(pixelSize * 0.5f, 0.01f) &&
			length(delta) < max(footprint * 2.0f, 0.02f) && dot(previousNormal, normal) > 0.9f) {

			float2 history = gShadowHistory[GetShadowOffset(previousPixel, slot)];
			count = min(GetShadowHistoryCount(history.y) + 1.0f, 16.0f);
			if (abs(current - history.x) > 0.25f) count = 1.0f;
			// 遮蔽物が動いた場所へ以前の影を残さない
			float variance = max(squaredSum / weightSum - current * current, 0.0f);
			float deviation = max(0.05f, 2.0f * sqrt(variance / weightSum));
			float lower = max(minimum, current - deviation);
			float upper = min(maximum, current + deviation);
			current = lerp(clamp(history.x, lower, upper), current, 1.0f / count);
			// フィルター幅も積算して影の揺れを防ぐ
			radius = lerp(GetShadowHistoryRadius(history.y), radius, 1.0f / count);
		}
	}
	gShadowOutput[offset] = float2(saturate(current), PackShadowHistory(radius, count));
}

[numthreads(8, 8, 1)]
void spatial(uint3 dispatchID : SV_DispatchThreadID) {

	uint2 pixel = dispatchID.xy;
	uint slot = dispatchID.z;
	if (any(pixel >= shadowSize) || slot >= shadowLightCount) return;
	uint offset = GetShadowOffset(pixel, slot);
	float2 center = gShadowInput[offset];
	float radius = GetShadowHistoryRadius(center.y);
	float4 position = gWorldPos.Load(int3(pixel, 0));
	float3 normal = normalize(gSurfaceNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
	float pixelSize = GetShadowPixelSize(position.xyz);
	float result = center.x;
	float propagatedRadius = radius;
	if (radius > 0.0f && position.w > 0.0f) {

		float sum = center.x;
		float weightSum = 1.0f;
		// 影の輪郭を保ちながら広い範囲へ標本を集める
		[unroll]
		for (int y = -1; y <= 1; ++y) {
			[unroll]
			for (int x = -1; x <= 1; ++x) {

				if (x == 0 && y == 0) continue;
				int2 samplePixel = int2(pixel) + int2(x, y) * int(shadowFilterStep);
				if (any(samplePixel < 0) || any(samplePixel >= int2(shadowSize))) continue;
				float2 sampleShadow = gShadowInput[GetShadowOffset(samplePixel, slot)];
				float sampleRadius = GetShadowHistoryRadius(sampleShadow.y);
				if (sampleRadius <= 0.0f) continue;
				float width = max(2.5f, max(radius, sampleRadius));
				float distanceSquared = float((x * x + y * y) * shadowFilterStep * shadowFilterStep);
				float weight = SurfaceWeight(position.xyz, normal, samplePixel, pixelSize) * exp(-distanceSquared / (width * width));
				// 明るい側にも半影の幅を引き継ぐ
				if (weight > 0.05f) propagatedRadius = max(propagatedRadius, sampleRadius);
				sum += sampleShadow.x * weight;
				weightSum += weight;
			}
		}
		result = sum / weightSum;
	}
	gShadowOutput[offset] = float2(saturate(result), PackShadowHistory(propagatedRadius, GetShadowHistoryCount(center.y)));
	if (slot == shadowPreviewLightIndex) gShadowPreview[pixel] = 1.0f - saturate(result);
}
