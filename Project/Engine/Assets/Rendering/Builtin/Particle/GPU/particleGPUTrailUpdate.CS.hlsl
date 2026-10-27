#include "particleGPUTrail.hlsli"

StructuredBuffer<ParticleParentGPU> gGPUParents : register(t0);
RWStructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(u0);
RWStructuredBuffer<uint> gGPUStatistics : register(u1);
RWStructuredBuffer<ParticleTrailGPU> gGPUTrails : register(u2);
RWStructuredBuffer<ParticleTrailPointGPU> gGPUTrailPoints : register(u3);
RWStructuredBuffer<uint> gGPUTrailFreeList : register(u4);
RWStructuredBuffer<uint> gGPURibbonOrder : register(u6);


void FreeTrail(uint index, inout ParticleTrailGPU trail) {
	trail.active = 0;
	trail.count = 0;
	if (mode != 0 || trail.generation == 0xffffffffu) return;
	++trail.generation;
	uint freeIndex;
	InterlockedAdd(gGPUTrailFreeList[0], 1u, freeIndex);
	gGPUTrailFreeList[freeIndex + 1] = index;
}

bool OwnerAlive(ParticleTrailGPU trail) {
	if (mode == 1u) return gGPUStatistics[0] != 0;
	if (trail.ownerIndex >= particleCapacity) return false;
	ParticleAttributesGPU owner = gGPUParticles[trail.ownerIndex];
	return owner.alive != 0 && owner.birthLow == trail.ownerBirthLow && owner.birthHigh == trail.ownerBirthHigh;
}

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID, uint localIndex : SV_GroupIndex) {
	if (trailOperation == 0u) {
		if (index < trailCapacity) {
			ParticleTrailGPU trail = (ParticleTrailGPU) 0;
			trail.ownerIndex = 0xffffffffu;
			trail.generation = 1;
			gGPUTrails[index] = trail;
			gGPUTrailFreeList[index + 1] = index;
		}
		if (index == 0) gGPUTrailFreeList[0] = trailCapacity;
		return;
	}
	if (trailOperation == 1u && index < trailCapacity) {
		ParticleTrailGPU trail = gGPUTrails[index];
		if (trail.active == 0) return;
		bool alive = OwnerAlive(trail);
		if (!alive && keepAfterDeath == 0) FreeTrail(index, trail);
		else if (mode == 1u) {
			if (!alive) {
				trail.head.positionAge.w += deltaTime;
				for (uint i = 0; i < trail.count; ++i) gGPUTrailPoints[gGPURibbonOrder[i]].positionAge.w += deltaTime;
				if (trail.head.positionAge.w >= lifetime) FreeTrail(index, trail);
			}
		} else {
			for (uint i = 0; i < trail.count; ++i) gGPUTrailPoints[index * pointCapacity + (trail.begin + i) % pointCapacity].positionAge.w += deltaTime;
			if (!alive) trail.head.positionAge.w += deltaTime;
			while (trail.count != 0 && gGPUTrailPoints[index * pointCapacity + trail.begin].positionAge.w >= lifetime) {
				trail.begin = (trail.begin + 1) % pointCapacity;
				--trail.count;
			}
			if (!alive && trail.count == 0) FreeTrail(index, trail);
		}
		gGPUTrails[index] = trail;
		return;
	}
	if (trailOperation == 2u && mode == 0 && index < particleCapacity) {
		ParticleAttributesGPU particle = gGPUParticles[index];
		if (particle.alive == 0 || (particle.trailIndex == 0xffffffffu && particle.trailGeneration == 0xffffffffu)) return;
		if (particle.trailIndex < trailCapacity) {
			ParticleTrailGPU current = gGPUTrails[particle.trailIndex];
			if (current.active != 0 && current.generation == particle.trailGeneration && current.ownerIndex == index &&
				current.ownerBirthLow == particle.birthLow && current.ownerBirthHigh == particle.birthHigh) return;
		}
		uint trailIndex = 0xffffffffu;
		uint freeCount;
		InterlockedAdd(gGPUTrailFreeList[0], 0xffffffffu, freeCount);
		if (freeCount != 0 && freeCount <= trailCapacity) trailIndex = gGPUTrailFreeList[freeCount];
		// 上限を超えた引落しだけ戻す
		else InterlockedAdd(gGPUTrailFreeList[0], 1u);
		particle.trailIndex = trailIndex;
		particle.trailGeneration = 0xffffffffu;
		if (trailIndex < trailCapacity) {
			ParticleTrailGPU trail = gGPUTrails[trailIndex];
			trail.active = 1;
			trail.begin = 0;
			trail.count = 0;
			trail.ownerIndex = index;
			trail.ownerBirthLow = particle.birthLow;
			trail.ownerBirthHigh = particle.birthHigh;
			particle.trailGeneration = trail.generation;
			gGPUTrails[trailIndex] = trail;
		} else InterlockedAdd(gGPUStatistics[3], 1u);
		gGPUParticles[index] = particle;
		return;
	}
	if (trailOperation == 3u && mode == 0 && index < trailCapacity) {
		ParticleTrailGPU trail = gGPUTrails[index];
		if (trail.active == 0 || !OwnerAlive(trail)) return;
		ParticleAttributesGPU owner = gGPUParticles[trail.ownerIndex];
		ParticleTrailPointGPU trailPoint = MakeParticleTrailPoint(owner, gGPUParents[owner.phase]);
		bool append = trail.count == 0;
		if (trail.count != 0) {
			ParticleTrailPointGPU last = gGPUTrailPoints[index * pointCapacity + (trail.begin + trail.count - 1) % pointCapacity];
			float distance = length(trailPoint.positionAge.xyz - last.positionAge.xyz);
			trailPoint.distance = last.distance + distance;
			append = distance >= minimumDistance;
		}
		trail.head = trailPoint;
		if (append) {
			if (trail.count == pointCapacity) { trail.begin = (trail.begin + 1) % pointCapacity; --trail.count; }
			gGPUTrailPoints[index * pointCapacity + (trail.begin + trail.count) % pointCapacity] = trailPoint;
			++trail.count;
		}
		gGPUTrails[index] = trail;
		return;
	}
	if (trailOperation == 9u && index < trailCapacity && gGPUTrails[index].active != 0) InterlockedAdd(gGPUStatistics[1], 1u);
}
