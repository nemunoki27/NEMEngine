//============================================================================
//	include
//============================================================================
#include "inlineShadowParameters.hlsli"

Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gWorldPos : register(t2);
Texture2D<float2> gMotion : register(t3);
StructuredBuffer<uint> gShadowInput : register(t20);
StructuredBuffer<float2> gShadowRaw : register(t21);
StructuredBuffer<uint> gShadowHistory : register(t22);
Texture2D<float4> gPreviousPosition : register(t23);
Texture2D<float4> gPreviousNormal : register(t24);
Texture2D<float4> gSurfaceNormal : register(t25);

RWStructuredBuffer<uint> gShadowOutput : register(u0);
RWTexture2D<float4> gCurrentPosition : register(u1);
RWTexture2D<float4> gCurrentNormal : register(u2);
RWTexture2D<float> gShadowPreview : register(u3);

groupshared float4 sharedPosition[144];
groupshared float3 sharedNormal[144];
groupshared float2 sharedRaw[144];
groupshared uint sharedTemporalUniform;

groupshared float4 sharedSpatialPosition[144];
groupshared float3 sharedSpatialNormal[144];
groupshared uint sharedSpatialShadow[144];
groupshared uint sharedSpatialActive;

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
	uint threadIndex = groupThread.y * 8u + groupThread.x;
	bool uniformRaw = true;
	float localOcclusion = 0.0f;
	const float maximumPosition = sqrt(3.402823466e38f) * 0.125f;
	for (uint index = threadIndex; index < 144u; index += 64u) {

		int2 samplePixel = clamp(int2(groupID.xy * 8u) + int2(index % 12u, index / 12u) - 2,
			int2(0, 0), int2(shadowSize) - 1);
		sharedPosition[index] = gWorldPos.Load(int3(samplePixel, 0));
		sharedNormal[index] = normalize(gNormal.Load(int3(samplePixel, 0)).xyz * 2.0f - 1.0f);
		sharedRaw[index] = gShadowRaw[GetShadowOffset(samplePixel, dispatchID.z)];
		if (index == threadIndex) localOcclusion = sharedRaw[index].x;
		uniformRaw = uniformRaw && sharedRaw[index].x == localOcclusion &&
			(localOcclusion == 0.0f || localOcclusion == 1.0f) && sharedRaw[index].y == kMinimumShadowFilterRadius &&
			all(isfinite(sharedPosition[index])) && all(abs(sharedPosition[index].xyz) <= maximumPosition) &&
			all(isfinite(sharedNormal[index]));
	}
	if (threadIndex == 0u) sharedTemporalUniform = 1u;
	GroupMemoryBarrierWithGroupSync();
	if (!uniformRaw || localOcclusion != sharedRaw[0].x) InterlockedAnd(sharedTemporalUniform, 0u);
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
		gShadowOutput[offset] = EncodeShadowValue(float2(raw.x, PackShadowHistory(0.0f, 1.0f))); return;
	}
	float pixelSize = GetShadowPixelSize(position.xyz);
	float2 uv = (float2(pixel) + 0.5f) / float2(shadowSize);
	float2 motion = gMotion.Load(int3(pixel, 0));
	float4 previousClip = mul(float4(position.xyz, 1.0f), shadowPreviousViewProjection);
	float2 previousUV = dot(motion, motion) > 1e-12f ? uv - motion :
		previousClip.xy / max(previousClip.w, 0.0001f) * float2(0.5f, -0.5f) + 0.5f;
	bool historyCandidate = false;
	float3 historyDelta = 0.0f.xxx;
	float2 history = 0.0f.xx;
	if (resetShadowHistory == 0u && previousClip.w > 0.0f && all(previousUV > 0.0f.xx) && all(previousUV < 1.0f.xx)) {

		int2 previousPixel = min(int2(previousUV * shadowSize), int2(shadowSize) - 1);
		float4 previousPosition = gPreviousPosition.Load(int3(previousPixel, 0));
		float3 previousNormal = normalize(gPreviousNormal.Load(int3(previousPixel, 0)).xyz * 2.0f - 1.0f);
		historyDelta = previousPosition.xyz - position.xyz;
		historyCandidate = previousPosition.w > 0.0f && abs(dot(historyDelta, normal)) < max(pixelSize * 0.5f, 0.01f) &&
			dot(previousNormal, normal) > 0.9f;
		if (historyCandidate) history = DecodeShadowValue(gShadowHistory[GetShadowOffset(previousPixel, slot)]);
	}
	float normalLengthSquared = dot(normal, normal);
	bool uniformNeighborhood = sharedTemporalUniform != 0u && all(isfinite(normal)) && isfinite(pixelSize) &&
		normalLengthSquared > 0.5f && normalLengthSquared < 1.5f &&
		(!historyCandidate || GetShadowHistoryRadius(history.y) == kMinimumShadowFilterRadius);
	float sum = 0.0f;
	float squaredSum = 0.0f;
	float radiusSum = 0.0f;
	float weightSum = 0.0f;
	float minimum = 1.0f;
	float maximum = 0.0f;
	float footprint = pixelSize;

	if (uniformNeighborhood) {

		// 一定の遮蔽率と最小半影幅では再投影の許容距離だけを求める
		sum = raw.x;
		squaredSum = raw.x;
		radiusSum = kMinimumShadowFilterRadius;
		weightSum = 1.0f;
		minimum = raw.x;
		maximum = raw.x;
		const int2 directions[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
		[unroll]
		for (uint direction = 0u; direction < 4u; ++direction) {

			int2 delta = directions[direction];
			int2 samplePixel = int2(pixel) + delta;
			if (any(samplePixel < 0) || any(samplePixel >= int2(shadowSize))) continue;
			uint sampleIndex = uint(int(centerIndex) + delta.y * 12 + delta.x);
			float weight = SurfaceWeightFromGeometry(position.xyz, normal, sharedPosition[sampleIndex],
				sharedNormal[sampleIndex], pixelSize) * exp(-0.25f);
			if (weight < 0.01f) continue;
			footprint = max(footprint, length(sharedPosition[sampleIndex].xyz - position.xyz));
		}
	} else {

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
	}
	float current = sum / max(weightSum, 0.0001f);
	float radius = radiusSum / max(weightSum, 0.0001f);
	float count = 1.0f;
	// 再投影先の位置と法線が一致する履歴だけを使う
	if (historyCandidate && length(historyDelta) < max(footprint * 2.0f, 0.02f)) {

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
	gShadowOutput[offset] = EncodeShadowValue(float2(saturate(current), PackShadowHistory(radius, count)));
}

[numthreads(8, 8, 1)]
void spatial(uint3 dispatchID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThread : SV_GroupThreadID) {

	uint2 pixel = dispatchID.xy;
	uint slot = dispatchID.z;
	bool valid = all(pixel < shadowSize) && slot < shadowLightCount;
	uint offset = GetShadowOffset(pixel, slot);
	uint packedCenter = valid ? gShadowInput[offset] : 0u;
	float2 center = DecodeShadowValue(packedCenter);
	float radius = GetPackedShadowRadius(packedCenter);
	bool active = valid && radius > 0.0f;
	bool useShared = shadowFilterStep <= 2u;
	uint tileWidth = 8u + shadowFilterStep * 2u;
	uint centerIndex = (groupThread.y + shadowFilterStep) * tileWidth + groupThread.x + shadowFilterStep;
	uint threadIndex = groupThread.y * 8u + groupThread.x;
	if (useShared) {

		if (threadIndex == 0u) sharedSpatialActive = 0u;
		GroupMemoryBarrierWithGroupSync();
		if (active) InterlockedOr(sharedSpatialActive, 1u);
		GroupMemoryBarrierWithGroupSync();
		// 半影のないGroupでは周辺の影も読み込まない
		if (sharedSpatialActive != 0u) {

			uint reference = gShadowInput[GetShadowOffset(groupID.xy * 8u, slot)] & 0xffe0ffffu;
			bool different = (reference & 65535u) != 0u && (reference & 65535u) != 65535u;
			for (uint index = threadIndex; index < tileWidth * tileWidth; index += 64u) {

				int2 samplePixel = clamp(int2(groupID.xy * 8u) + int2(index % tileWidth, index / tileWidth) - int(shadowFilterStep),
					int2(0, 0), int2(shadowSize) - 1);
				uint shadow = gShadowInput[GetShadowOffset(samplePixel, slot)];
				sharedSpatialShadow[index] = shadow;
				different = different || (shadow & 0xffe0ffffu) != reference;
			}
			if (different) InterlockedOr(sharedSpatialActive, 2u);
			GroupMemoryBarrierWithGroupSync();
			if (sharedSpatialActive == 3u) {

				// 隣接画素で重複する位置・法線の読み込みを共有する
				for (uint index = threadIndex; index < tileWidth * tileWidth; index += 64u) {

					int2 samplePixel = clamp(int2(groupID.xy * 8u) + int2(index % tileWidth, index / tileWidth) - int(shadowFilterStep),
						int2(0, 0), int2(shadowSize) - 1);
					sharedSpatialPosition[index] = gWorldPos.Load(int3(samplePixel, 0));
					sharedSpatialNormal[index] = normalize(gNormal.Load(int3(samplePixel, 0)).xyz * 2.0f - 1.0f);
				}
				GroupMemoryBarrierWithGroupSync();
			}
		}
	}
	if (!valid) return;
	float result = center.x;
	float propagatedRadius = radius;
	if (active && (!useShared || sharedSpatialActive == 3u)) {

		uint samples[9];
		bool uniformShadow = (packedCenter & 65535u) == 0u || (packedCenter & 65535u) == 65535u;
		[unroll]
		for (int y = -1; y <= 1; ++y) {
			[unroll]
			for (int x = -1; x <= 1; ++x) {

				if (x == 0 && y == 0) continue;
				int2 samplePixel = int2(pixel) + int2(x, y) * int(shadowFilterStep);
				if (any(samplePixel < 0) || any(samplePixel >= int2(shadowSize))) continue;
				uint sampleIndex = uint(int(centerIndex) + (y * int(tileWidth) + x) * int(shadowFilterStep));
				uint shadow;
				[branch]
				if (useShared) shadow = sharedSpatialShadow[sampleIndex];
				else shadow = gShadowInput[GetShadowOffset(samplePixel, slot)];
				samples[(y + 1) * 3 + x + 1] = shadow;
				uniformShadow = uniformShadow && ((shadow >> 21u) == 0u ||
					((shadow & 65535u) == (packedCenter & 65535u) && (shadow >> 21u) <= (packedCenter >> 21u)));
			}
		}
		// 遮蔽率が一定で半影幅も増えない画素は幾何情報を評価しない
		float4 position = 0.0f.xxxx;
		if (!uniformShadow) {
			[branch]
			if (useShared) position = sharedSpatialPosition[centerIndex];
			else position = gWorldPos.Load(int3(pixel, 0));
		}
		if (position.w > 0.0f) {

			float3 normal = normalize(gSurfaceNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
			float pixelSize = GetShadowPixelSize(position.xyz);
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
					uint sampleIndex = uint(int(centerIndex) + (y * int(tileWidth) + x) * int(shadowFilterStep));
					uint packedShadow = samples[(y + 1) * 3 + x + 1];
					float2 sampleShadow = DecodeShadowValue(packedShadow);
					float sampleRadius = GetPackedShadowRadius(packedShadow);
					if (sampleRadius <= 0.0f) continue;
					float width = max(2.5f, max(radius, sampleRadius));
					float distanceSquared = float((x * x + y * y) * shadowFilterStep * shadowFilterStep);
					float surfaceWeight;
					[branch]
					if (useShared) surfaceWeight = SurfaceWeightFromGeometry(position.xyz, normal,
						sharedSpatialPosition[sampleIndex], sharedSpatialNormal[sampleIndex], pixelSize);
					else surfaceWeight = SurfaceWeight(position.xyz, normal, samplePixel, pixelSize);
					float weight = surfaceWeight * exp(-distanceSquared / (width * width));
					// 明るい側にも半影の幅を引き継ぐ
					if (weight > 0.05f) propagatedRadius = max(propagatedRadius, sampleRadius);
					sum += sampleShadow.x * weight;
					weightSum += weight;
				}
			}
			result = sum / weightSum;
		}
	}
	gShadowOutput[offset] = EncodeShadowValue(float2(saturate(result), PackShadowHistory(propagatedRadius, GetPackedShadowCount(packedCenter))));
	if (slot == shadowPreviewLightIndex) gShadowPreview[pixel] = 1.0f - saturate(result);
}
