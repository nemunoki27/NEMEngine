#ifndef NEM_GI_PROBE_SAMPLING_HLSLI
#define NEM_GI_PROBE_SAMPLING_HLSLI

// Camera別の確定済みProbeを参照
cbuffer GlobalIlluminationConstants : register(b0, space6) {

	float4 giGridOrigins[3];
	uint giGridCount;
	uint giLevelCount;
	uint giStartProbe;
	uint giUpdateCount;
	uint giRayCount;
	uint giFrameIndex;
	uint giResetHistory;
	uint giEnabled;
	float giMaxRayDistance;
	float giHysteresis;
	float giNormalBias;
	float giViewBias;
	uint giDebugMode;
	uint3 giPadding;
};
Texture2D<float4> gGIIrradiance : register(t0, space6);
Texture2D<float4> gGIDistance : register(t1, space6);
Texture2D<float4> gGIPositions : register(t2, space6);
Texture2D<float4> gGIOffsets : register(t3, space6);

uint GIProbeIndex(int3 cell, uint level) {

	int count = int(giGridCount);
	uint3 slot = uint3((cell % count + count) % count);
	return level * giGridCount * giGridCount * giGridCount +
		slot.x + giGridCount * (slot.y + giGridCount * slot.z);
}

float2 GIOctEncode(float3 direction) {

	direction /= max(dot(abs(direction), 1.0f.xxx), 1e-6f);
	float2 uv = direction.xy;
	if (direction.z < 0.0f) uv = (1.0f - abs(uv.yx)) * float2(uv.x >= 0.0f ? 1.0f : -1.0f, uv.y >= 0.0f ? 1.0f : -1.0f);
	return uv * 0.5f + 0.5f;
}

float3 GIOctDecode(uint index) {

	float2 uv = (float2(index % 8u, index / 8u) + 0.5f) / 8.0f * 2.0f - 1.0f;
	float3 direction = float3(uv, 1.0f - abs(uv.x) - abs(uv.y));
	float correction = saturate(-direction.z);
	direction.xy += float2(direction.x >= 0.0f ? -correction : correction, direction.y >= 0.0f ? -correction : correction);
	return normalize(direction);
}

uint GIDirectionBin(float3 direction) {

	uint2 pixel = min(uint2(GIOctEncode(direction) * 8.0f), 7u.xx);
	return pixel.x + pixel.y * 8u;
}

// 八面体の継ぎ目を折り返して方向を補間
float4 GILoadDirection(Texture2D<float4> field, uint probeIndex, float3 direction) {

	float2 coordinate = GIOctEncode(direction) * 8.0f - 0.5f;
	int2 first = int2(floor(coordinate));
	float2 fraction = frac(coordinate);
	float4 result = 0.0f.xxxx;
	[unroll]
	for (uint corner = 0u; corner < 4u; ++corner) {

		int2 offset = int2(corner & 1u, corner >> 1u);
		int2 pixel = first + offset;
		if (pixel.x < 0 || pixel.x > 7) {
			pixel.x = pixel.x < 0 ? -pixel.x - 1 : 15 - pixel.x;
			pixel.y = 7 - pixel.y;
		}
		if (pixel.y < 0 || pixel.y > 7) {
			pixel.y = pixel.y < 0 ? -pixel.y - 1 : 15 - pixel.y;
			pixel.x = 7 - pixel.x;
		}
		float2 weight = lerp(1.0f - fraction, fraction, float2(offset));
		result += field.Load(int3(probeIndex, pixel.x + pixel.y * 8, 0)) * weight.x * weight.y;
	}
	return result;
}

float3 GIRayDirection(uint rayIndex) {

	// Fibonacci球面をframeごとに回転
	float z = 1.0f - 2.0f * (float(rayIndex) + 0.5f) / float(giRayCount);
	float angle = float(rayIndex) * 2.39996323f + float(giFrameIndex % 1024u) * 0.754877666f;
	float radius = sqrt(max(1.0f - z * z, 0.0f));
	return float3(cos(angle) * radius, sin(angle) * radius, z);
}

float3 GINominalProbePosition(uint probeIndex) {

	uint perLevel = giGridCount * giGridCount * giGridCount;
	uint level = probeIndex / perLevel;
	uint slot = probeIndex % perLevel;
	int3 ringCell = int3(slot % giGridCount, (slot / giGridCount) % giGridCount, slot / (giGridCount * giGridCount));
	float spacing = giGridOrigins[level].w;
	int3 firstCell = int3(round(giGridOrigins[level].xyz / spacing));
	int3 local = (ringCell - firstCell) % int(giGridCount);
	local = (local + int(giGridCount)) % int(giGridCount);
	return float3(firstCell + local) * spacing;
}

float3 GIDiffuseContribution(float4 indirect, float3 albedo, float metallic, float ao) {

	// Probeの有効率と拡散反射率を描画へ反映
	return indirect.rgb * indirect.a * albedo * ao * (1.0f - metallic) * 0.96f / 3.14159265359f;
}

float4 SampleGlobalIllumination(float3 worldPosition, float3 normal, float3 viewDirection) {

	if (giEnabled == 0u) return 0.0f.xxxx;
	// 遮蔽判定位置を視線方向で動かさない
	float3 queryPosition = worldPosition + normal * giNormalBias;
	float3 result = 0.0f.xxx;
	float coverage = 0.0f;
	[unroll]
	for (uint level = 0u; level < 3u; ++level) {

		// 細かい格子で確定した画素は粗い格子を読まない
		if (level >= giLevelCount || coverage >= 0.999f) break;
		float spacing = giGridOrigins[level].w;
		float3 local = (queryPosition - giGridOrigins[level].xyz) / spacing;
		if (any(local < 0.0f) || any(local >= float(giGridCount - 1u))) continue;
		float3 fraction = frac(queryPosition / spacing);
		int3 firstCell = int3(floor(queryPosition / spacing));
		float3 accumulated = 0.0f.xxx;
		float weightSum = 0.0f;
		float validWeight = 0.0f;
		[unroll]
		for (uint corner = 0u; corner < 8u; ++corner) {

			int3 offset = int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
			int3 cell = firstCell + offset;
			uint probeIndex = GIProbeIndex(cell, level);
			float4 storedPosition = gGIPositions.Load(int3(probeIndex, 0, 0));
			if (storedPosition.w <= 0.0f || any(abs(storedPosition.xyz - float3(cell) * spacing) > spacing * 0.01f)) continue;
			float4 relocation = gGIOffsets.Load(int3(probeIndex, 0, 0));
			if (relocation.w <= 0.0f) continue;
			float3 toPoint = queryPosition - storedPosition.xyz - relocation.xyz;
			float distanceToPoint = length(toPoint);
			float3 direction = toPoint / max(distanceToPoint, 1e-5f);
			float2 moments = GILoadDirection(gGIDistance, probeIndex, direction).xy;
			float variance = max(moments.y - moments.x * moments.x, spacing * spacing * 1e-5f);
			float excess = max(distanceToPoint - moments.x, 0.0f);
			float visibility = variance / (variance + excess * excess);
			visibility *= visibility * visibility;
			float3 axisWeight = lerp(1.0f - fraction, fraction, float3(offset));
			float trilinearWeight = axisWeight.x * axisWeight.y * axisWeight.z;
			float facing = max(0.05f, dot(normal, -direction) * 0.5f + 0.5f);
			float weight = trilinearWeight * facing * facing * max(visibility, 1e-5f);
			float3 irradiance = GILoadDirection(gGIIrradiance, probeIndex, normal).rgb;
			accumulated += irradiance * weight;
			weightSum += weight;
			validWeight += trilinearWeight * min(storedPosition.w / 3.0f, 1.0f);
		}
		if (weightSum > 1e-10f) {

			float edge = min(min(local.x, local.y), min(local.z,
				min(float(giGridCount - 1u) - local.x, min(float(giGridCount - 1u) - local.y, float(giGridCount - 1u) - local.z))));
			float blend = smoothstep(0.0f, 2.0f, edge) * saturate(validWeight) * (1.0f - coverage);
			result += accumulated / weightSum * blend;
			coverage += blend;
		}
	}
	return float4(max(result, 0.0f.xxx) / max(coverage, 1e-6f), coverage);
}
#endif
