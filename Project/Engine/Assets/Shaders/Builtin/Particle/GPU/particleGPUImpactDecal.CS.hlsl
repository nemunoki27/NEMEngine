#include "particleGPUEvents.hlsli"
#include "../../Decal/decalCommon.hlsli"

cbuffer ParticleGPUImpactDecalConstants : register(b1, space4) {
	uint impactCapacity;
	uint impactStyleCount;
	uint impactOperation;
	float impactDeltaTime;
	uint impactPreviousCapacity;
	uint impactWordCount;
	uint impactSummaryCount;
	uint impactReserved;
};

StructuredBuffer<DecalGPUData> gGPUImpactDecalStyles : register(t8, space4);
RWStructuredBuffer<ParticleImpactDecalGPU> gGPUImpactDecalsOut : register(u8, space4);
RWStructuredBuffer<uint> gGPUImpactDecalFreeList : register(u9, space4);
RWStructuredBuffer<uint4> gGPUImpactDecalArguments : register(u10, space4);
RWStructuredBuffer<uint> gGPUImpactDecalBinsOut : register(u12, space4);
RWStructuredBuffer<uint> gGPUImpactDecalBinWordsOut : register(u13, space4);

uint AllocateImpactDecal() {
	uint observed = gGPUImpactDecalFreeList[0];
	while (observed != 0) {
		uint prior;
		InterlockedCompareExchange(gGPUImpactDecalFreeList[0], observed, observed - 1, prior);
		if (prior == observed) return gGPUImpactDecalFreeList[observed];
		observed = prior;
	}
	return 0xffffffffu;
}

DecalGPUData BuildImpactDecal(DecalGPUData style, ParticleGPUEvent impact) {
	float3 z = -ParticleNormalize(impact.normalHardness.xyz, float3(0, 1, 0));
	float3 x = ParticleNormalize(cross(abs(z.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), z), float3(1, 0, 0));
	float3 y = cross(z, x);
	float3 size = float3(style.tangentX.w, style.tangentY.w, style.direction.w);
	float3 center = impact.positionRadius.xyz + z * size.z * 0.45f;
	style.worldToDecal = float4x4(
		x.x / size.x, y.x / size.y, z.x / size.z, 0,
		x.y / size.x, y.y / size.y, z.y / size.z, 0,
		x.z / size.x, y.z / size.y, z.z / size.z, 0,
		-dot(center, x) / size.x, -dot(center, y) / size.y, -dot(center, z) / size.z, 1);
	style.tangentX.xyz = x * size.x;
	style.tangentY.xyz = y * size.y;
	style.direction.xyz = z * size.z;
	style.baseColor *= impact.color;
	return style;
}

// 同じbucketへ重なる登録はbitの論理和でまとめる
void RegisterImpactDecalBin(uint slot, uint bin) {
	const uint word = slot / 32u;
	InterlockedOr(gGPUImpactDecalBinsOut[bin * impactWordCount + word], 1u << (slot & 31u));
	InterlockedOr(gGPUImpactDecalBinWordsOut[bin * impactSummaryCount + word / 32u], 1u << (word & 31u));
}

// 大きな投影だけを共通候補へ分けて登録量を制限する
void RegisterImpactDecal(uint slot) {
	ParticleImpactDecalGPU item = gGPUImpactDecalsOut[slot];
	if (item.alive == 0) return;
	float3 extent = (abs(item.decal.tangentX.xyz) + abs(item.decal.tangentY.xyz) + abs(item.decal.direction.xyz)) * 0.5f;
	int3 firstCell = int3(floor((item.center - extent) * 0.5f));
	int3 lastCell = int3(floor((item.center + extent) * 0.5f));
	if (any(lastCell - firstCell > 1)) {
		RegisterImpactDecalBin(slot, 256u);
		return;
	}
	for (int z = firstCell.z; z <= lastCell.z; ++z)
		for (int y = firstCell.y; y <= lastCell.y; ++y)
			for (int x = firstCell.x; x <= lastCell.x; ++x)
				RegisterImpactDecalBin(slot, ParticleImpactDecalBin(int3(x, y, z)));
}

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (impactOperation == 7u) {
		if (index < 257u * impactWordCount) gGPUImpactDecalBinsOut[index] = 0;
		if (index < 257u * impactSummaryCount) gGPUImpactDecalBinWordsOut[index] = 0;
		return;
	}
	if (impactOperation == 8u) {
		if (index < impactCapacity) RegisterImpactDecal(index);
		return;
	}
	if (impactOperation == 6u) {
		if (index == 0) gGPUImpactDecalFreeList[0] = 0;
		return;
	}
	if (impactOperation == 5u) {
		if (index >= impactCapacity) return;
		if (index >= impactPreviousCapacity) gGPUImpactDecalsOut[index] = (ParticleImpactDecalGPU) 0;
		if (gGPUImpactDecalsOut[index].alive == 0) {
			uint claim;
			InterlockedAdd(gGPUImpactDecalFreeList[0], 1u, claim);
			gGPUImpactDecalFreeList[claim + 1] = index;
		}
		return;
	}
	if (impactOperation == 0u) {
		if (index >= impactCapacity) return;
		gGPUImpactDecalsOut[index] = (ParticleImpactDecalGPU) 0;
		gGPUImpactDecalFreeList[index + 1] = index;
		if (index == 0) gGPUImpactDecalArguments[0] = uint4(3, impactCapacity, 0, 0);
		if (index == 0) gGPUImpactDecalFreeList[0] = impactCapacity;
		return;
	}
	if (impactOperation == 1u) {
		if (index >= impactCapacity) return;
		ParticleImpactDecalGPU item = gGPUImpactDecalsOut[index];
		if (item.alive == 0) return;
		item.ageLifetimeFade.x += impactDeltaTime;
		if (item.ageLifetimeFade.x >= item.ageLifetimeFade.y) {
			item.alive = 0;
			uint claim;
			InterlockedAdd(gGPUImpactDecalFreeList[0], 1u, claim);
			gGPUImpactDecalFreeList[claim + 1] = index;
		} else {
			float fade = item.ageLifetimeFade.z <= 0 ? 1 : saturate((item.ageLifetimeFade.y - item.ageLifetimeFade.x) / item.ageLifetimeFade.z);
			item.decal.baseColor.a = item.ageLifetimeFade.w * fade;
		}
		gGPUImpactDecalsOut[index] = item;
		return;
	}
	if (impactOperation == 2u) {
		if (index >= min(gGPUEventCounts[0], eventCapacity)) return;
		ParticleGPUEvent impact = gGPUEvents[index];
		uint style = impact.flags >> 16u;
		if ((impact.flags & 192u) != 128u || style >= impactStyleCount) return;
		uint slot = AllocateImpactDecal();
		if (slot == 0xffffffffu) {
			InterlockedAdd(gGPUFeedbackStatistics[3], 1u);
			return;
		}
		ParticleImpactDecalGPU item = (ParticleImpactDecalGPU) 0;
		item.decal = BuildImpactDecal(gGPUImpactDecalStyles[style], impact);
		item.ageLifetimeFade = float4(0, impact.velocityLifetime.w, impact.normalHardness.w, item.decal.baseColor.a);
		item.alive = 1;
		item.center = impact.positionRadius.xyz + item.decal.direction.xyz * 0.45f;
		gGPUImpactDecalsOut[slot] = item;
		return;
	}
	if (impactOperation == 3u) {
		if (index == 0) gGPUFeedbackStatistics[6] = 0;
		return;
	}
	if (index >= impactCapacity) return;
	uint alive = gGPUImpactDecalsOut[index].alive;
	if (index == 0) gGPUImpactDecalArguments[0] = uint4(3, impactCapacity, 0, 0);
	if (alive != 0) InterlockedAdd(gGPUFeedbackStatistics[6], 1u);
}
