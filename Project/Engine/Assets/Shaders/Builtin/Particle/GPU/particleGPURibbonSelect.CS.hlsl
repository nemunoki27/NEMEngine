#include "particleGPUTrail.hlsli"

StructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(t0);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t1);
StructuredBuffer<uint4> gGPURibbonCandidatesIn : register(t2);
StructuredBuffer<uint> gGPUStatistics : register(t3);
RWStructuredBuffer<uint4> gGPURibbonCandidatesOut : register(u0);
RWStructuredBuffer<ParticleTrailGPU> gGPUTrails : register(u1);
RWStructuredBuffer<ParticleTrailPointGPU> gGPUTrailPoints : register(u2);
RWStructuredBuffer<uint> gGPURibbonOrder : register(u3);

groupshared uint4 candidates[2048];

bool After(uint4 a, uint4 b) {
	if (a.w != b.w) return a.w < b.w;
	return a.x < b.x || (a.x == b.x && a.y < b.y);
}

[numthreads(256, 1, 1)]
void main(uint thread : SV_GroupIndex, uint3 group : SV_GroupID) {
	if (gGPUStatistics[0] == 0) return;
	bool first = trailOperation == 10u || trailOperation == 13u;
	bool final = trailOperation == 12u || trailOperation == 13u;
	for (uint i = thread; i < 2048; i += 256) {
		uint index = group.x * 2048 + i;
		uint4 entry = 0u.xxxx;
		if (index < ribbonInputCount) {
			if (first) {
				ParticleAttributesGPU particle = gGPUParticles[index];
				entry = uint4(particle.birthHigh, particle.birthLow, index, particle.alive);
			} else entry = gGPURibbonCandidatesIn[index];
		}
		candidates[i] = entry;
	}
	GroupMemoryBarrierWithGroupSync();
	// 各段で新しい粒子だけを残し、全体のソート領域を作らない
	for (uint width = 2; width <= 2048; width <<= 1) {
		for (uint stride = width >> 1; stride > 0; stride >>= 1) {
			for (uint i = thread; i < 2048; i += 256) {
				uint other = i ^ stride;
				if (other <= i) continue;
				uint4 a = candidates[i];
				uint4 b = candidates[other];
				if (After(a, b) == ((i & width) == 0)) { candidates[i] = b; candidates[other] = a; }
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}
	if (!final) {
		for (uint i = thread; i < pointCapacity; i += 256) gGPURibbonCandidatesOut[group.x * pointCapacity + i] = candidates[i];
		return;
	}
	uint count = min(gGPUStatistics[0], pointCapacity);
	for (uint i = thread; i < count; i += 256) {
		uint particleIndex = candidates[count - 1 - i].z;
		ParticleAttributesGPU particle = gGPUParticles[particleIndex];
		gGPUTrailPoints[i] = MakeParticleTrailPoint(particle, gGPUParents[particle.phase]);
		gGPURibbonOrder[i] = i;
	}
	DeviceMemoryBarrierWithGroupSync();
	if (thread == 0) {
		float distance = 0;
		for (uint i = 0; i < count; ++i) {
			ParticleTrailPointGPU current = gGPUTrailPoints[i];
			if (i > 0) distance += length(current.positionAge.xyz - gGPUTrailPoints[i - 1].positionAge.xyz);
			current.distance = distance;
			gGPUTrailPoints[i] = current;
		}
		ParticleTrailGPU trail = gGPUTrails[0];
		trail.active = 1;
		trail.begin = 0;
		trail.count = count;
		trail.head = gGPUTrailPoints[count - 1];
		gGPUTrails[0] = trail;
	}
}
