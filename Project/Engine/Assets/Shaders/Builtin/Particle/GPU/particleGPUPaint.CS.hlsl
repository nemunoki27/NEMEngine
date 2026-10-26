#include "particleGPUEvents.hlsli"
#include "particlePaintRead.hlsli"

cbuffer ParticleGPUPaintConstants : register(b1, space4) {
	ParticlePaintReceiverGPU paintReceiver;
	uint paintResolution;
	uint paintTiles;
	uint paintOperation;
	uint paintTileCapacity;
};

RWTexture2D<float4> gParticlePaintOutput : register(u5, space4);
RWStructuredBuffer<uint> gParticlePaintTileCounts : register(u6, space4);
RWStructuredBuffer<uint> gParticlePaintTileEvents : register(u7, space4);

bool PaintEventUV(ParticleGPUEvent impact, out float2 uv, out float2 radius) {
	uv = impact.uv;
	radius = impact.positionRadius.w * paintReceiver.scaleOpacity.xy;
	if (paintReceiver.projection == 1u) {
		float2 worldScale = float2(length(float3(paintReceiver.worldToReceiver[0][0], paintReceiver.worldToReceiver[1][0],
			paintReceiver.worldToReceiver[2][0])), length(float3(paintReceiver.worldToReceiver[0][2],
			paintReceiver.worldToReceiver[1][2], paintReceiver.worldToReceiver[2][2])));
		radius *= worldScale;
		uv = mul(float4(impact.positionRadius.xyz, 1), paintReceiver.worldToReceiver).xz * paintReceiver.scaleOpacity.xy + 0.5f;
	} else if ((impact.flags & 32u) == 0u) return false;
	return all(uv + radius >= 0) && all(uv - radius <= 1);
}

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (paintOperation == 4u) {
		if (index >= min(gGPUEventCounts[0], eventCapacity)) return;
		ParticleGPUEvent impact = gGPUEvents[index];
		if ((impact.flags & 192u) == 64u && FindParticlePaintReceiver(impact.target.entity) == 0xffffffffu) {
			InterlockedAdd(gGPUFeedbackStatistics[5], 1u);
		}
		return;
	}
	if (paintOperation == 0u) {
		if (index < paintTiles * paintTiles) gParticlePaintTileCounts[index] = 0;
		return;
	}
	if (paintOperation == 1u) {
		if (index >= min(gGPUEventCounts[0], eventCapacity)) return;
		ParticleGPUEvent impact = gGPUEvents[index];
		if ((impact.flags & 192u) != 64u || any(impact.target.entity != paintReceiver.entity)) return;
		float2 uv, radius;
		if (!PaintEventUV(impact, uv, radius)) {
			InterlockedAdd(gGPUFeedbackStatistics[5], 1u);
			return;
		}
		int2 first = clamp(int2(floor((uv - radius) * paintTiles)), 0, int(paintTiles) - 1);
		int2 last = clamp(int2(floor((uv + radius) * paintTiles)), 0, int(paintTiles) - 1);
		for (int y = first.y; y <= last.y; ++y) {
			for (int x = first.x; x <= last.x; ++x) {
				uint tile = y * paintTiles + x, claim;
				InterlockedAdd(gParticlePaintTileCounts[tile], 1u, claim);
				if (claim < paintTileCapacity) gParticlePaintTileEvents[tile * paintTileCapacity + claim] = index;
				else InterlockedAdd(gGPUFeedbackStatistics[4], 1u);
			}
		}
		return;
	}
	if (index >= paintResolution * paintResolution) return;
	uint2 pixel = uint2(index % paintResolution, index / paintResolution);
	if (paintOperation == 3u) {
		gParticlePaintOutput[pixel] = 0;
		return;
	}
	float2 pixelUV = (float2(pixel) + 0.5f) / paintResolution;
	uint tile = (pixel.y / 16) * paintTiles + pixel.x / 16;
	uint count = min(gParticlePaintTileCounts[tile], paintTileCapacity);
	float3 weightedColor = 0;
	float weight = 0, transparency = 1;
	for (uint eventIndex = 0; eventIndex < count; ++eventIndex) {
		ParticleGPUEvent impact = gGPUEvents[gParticlePaintTileEvents[tile * paintTileCapacity + eventIndex]];
		float2 uv, radius;
		PaintEventUV(impact, uv, radius);
		float distance = length((pixelUV - uv) / max(radius, 1e-6f.xx));
		float alpha = saturate(impact.color.a) * (1 - smoothstep(min(impact.normalHardness.w, 0.999f), 1, distance));
		weightedColor += impact.color.rgb * alpha;
		weight += alpha;
		transparency *= 1 - alpha;
	}
	if (weight <= 0) return;
	float4 previous = gParticlePaintOutput[pixel];
	float alpha = 1 - transparency;
	float outputAlpha = alpha + previous.a * transparency;
	float3 premultiplied = weightedColor / weight * alpha + previous.rgb * previous.a * transparency;
	gParticlePaintOutput[pixel] = float4(premultiplied / max(outputAlpha, 1e-6f), outputAlpha);
}
