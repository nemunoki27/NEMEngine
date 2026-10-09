#include "giProbeSampling.hlsli"

Texture2D<float4> gGIRayResults : register(t4, space6);
RWTexture2D<float4> gGIOutputIrradiance : register(u0, space6);
RWTexture2D<float4> gGIOutputDistance : register(u1, space6);
RWTexture2D<float4> gGIOutputPositions : register(u2, space6);
RWTexture2D<float4> gGIOutputOffsets : register(u3, space6);

[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint bin : SV_GroupIndex) {

	if (group.x >= giUpdateCount) return;
	uint probeCount = giGridCount * giGridCount * giGridCount * giLevelCount;
	uint probeIndex = (giStartProbe + group.x) % probeCount;
	float3 position = GINominalProbePosition(probeIndex);
	float spacing = giGridOrigins[probeIndex / (giGridCount * giGridCount * giGridCount)].w;
	float4 oldPosition = gGIPositions.Load(int3(probeIndex, 0, 0));
	bool reused = giResetHistory == 0u && oldPosition.w > 0.0f && all(abs(position - oldPosition.xyz) < spacing * 0.01f);
	float4 oldOffset = reused ? gGIOffsets.Load(int3(probeIndex, 0, 0)) : float4(0.0f.xxx, 1.0f);
	float3 direction = GIOctDecode(bin);
	float3 irradiance = 0.0f.xxx;
	float2 moments = 0.0f.xx;
	float distanceWeight = 0.0f;
	uint backfaceCount = 0u;
	float nearestBackface = giMaxRayDistance;
	float3 escape = 0.0f.xxx;
	[loop]
	for (uint rayIndex = 0u; rayIndex < giRayCount; ++rayIndex) {

		float4 sample = gGIRayResults.Load(int3(rayIndex, group.x, 0));
		float3 rayDirection = GIRayDirection(rayIndex);
		bool backface = sample.w < 0.0f;
		if (backface) {
			++backfaceCount;
			if (-sample.w < nearestBackface) {
				nearestBackface = -sample.w;
				escape = rayDirection * (nearestBackface + spacing * 0.1f);
			}
		}
		float cosine = max(dot(direction, rayDirection), 0.0f);
		irradiance += sample.rgb * cosine;
		float weight = pow(cosine, 32.0f);
		float distanceValue = backface ? 0.0f : min(sample.w, giMaxRayDistance);
		moments += float2(distanceValue, distanceValue * distanceValue) * weight;
		distanceWeight += weight;
	}
	irradiance *= 12.5663706144f / float(giRayCount);
	moments = distanceWeight > 1e-6f ? moments / distanceWeight : float2(giMaxRayDistance, giMaxRayDistance * giMaxRayDistance);
	bool inside = backfaceCount * 4u > giRayCount;
	float historyWeight = reused && oldOffset.w > 0.0f && !inside ? giHysteresis : 0.0f;
	float3 previousIrradiance = reused ? gGIIrradiance.Load(int3(probeIndex, bin, 0)).rgb : 0.0f.xxx;
	float2 previousDistance = reused ? gGIDistance.Load(int3(probeIndex, bin, 0)).xy : moments;
	// 急変時は蓄積を弱めて古い照明を残さない
	float difference = length(irradiance - previousIrradiance);
	if (difference > max(length(previousIrradiance) * 0.5f, 0.1f)) historyWeight = min(historyWeight, 0.5f);
	gGIOutputIrradiance[uint2(probeIndex, bin)] = float4(lerp(irradiance, previousIrradiance, historyWeight), 1.0f);
	gGIOutputDistance[uint2(probeIndex, bin)] = float4(lerp(moments, previousDistance, historyWeight), 0.0f, 0.0f);
	if (bin == 0u) {

		float3 relocation = oldOffset.xyz;
		if (inside) {
			relocation += escape;
			float maximumOffset = spacing * 0.45f;
			relocation *= min(1.0f, maximumOffset / max(length(relocation), 1e-5f));
		}
		gGIOutputPositions[uint2(probeIndex, 0)] = float4(position, reused ? min(oldPosition.w + 1.0f, 255.0f) : 1.0f);
		gGIOutputOffsets[uint2(probeIndex, 0)] = float4(relocation, inside ? 0.0f : 1.0f);
	}
}
