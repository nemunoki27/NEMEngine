#ifndef NEM_PARTICLE_GPU_TRAIL_READ
#define NEM_PARTICLE_GPU_TRAIL_READ

StructuredBuffer<ParticleTrailGPU> gGPUTrails : register(t0);
StructuredBuffer<ParticleTrailPointGPU> gGPUTrailPoints : register(t1);
StructuredBuffer<uint> gGPURibbonOrder : register(t2);

uint ParticleTrailPointCount(ParticleTrailGPU trail) {
	return mode == 0 ? trail.count + 1 : trail.count;
}

ParticleTrailPointGPU ReadParticleTrailPoint(ParticleTrailGPU trail, uint trailIndex, uint ordinal) {
	if (mode == 1u) return gGPUTrailPoints[gGPURibbonOrder[ordinal]];
	if (ordinal == trail.count) return trail.head;
	return gGPUTrailPoints[trailIndex * pointCapacity + (trail.begin + ordinal) % pointCapacity];
}

#endif
