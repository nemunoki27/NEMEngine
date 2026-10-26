#ifndef NEM_PARTICLE_GPU_MATERIAL_CURVES
#define NEM_PARTICLE_GPU_MATERIAL_CURVES

#include "particleGPUCurve.hlsli"

#define ParticleGPURenderCurveSample ParticleGPUCurveSample
StructuredBuffer<uint4> gParticleMaterialCurveHeaders : register(t2, space1);
StructuredBuffer<ParticleGPUCurveSample> gParticleMaterialCurveSamples : register(t3, space1);

ParticleGPURenderCurveSample SampleParticleGPURenderCurve(uint index, float progress) {

	uint2 header = gParticleMaterialCurveHeaders[index].xy;
	if (header.y == 0u) return (ParticleGPUCurveSample) 0;
	return SampleParticleGPUCurve(gParticleMaterialCurveSamples, header.x, header.y, progress);
}

#endif
