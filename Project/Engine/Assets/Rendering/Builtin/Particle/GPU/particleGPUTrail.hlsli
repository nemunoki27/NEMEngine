#ifndef NEM_PARTICLE_GPU_TRAIL
#define NEM_PARTICLE_GPU_TRAIL
#include "particleGPUShared.hlsli"

#ifndef NEM_TRAIL_CONSTANT_REGISTER
#define NEM_TRAIL_CONSTANT_REGISTER b0
#endif

cbuffer ParticleGPUTrailConstants : register(NEM_TRAIL_CONSTANT_REGISTER) {
	uint trailCapacity;
	uint pointCapacity;
	uint particleCapacity;
	uint trailOperation;
	uint mode;
	uint keepAfterDeath;
	uint textureMode;
	uint ribbonSortCount;
	float minimumDistance;
	float lifetime;
	float deltaTime;
	float tileLength;
	float startWidth;
	float endWidth;
	uint pointModuloHighFactor;
	uint ribbonInputCount;
	float4 startColor;
	float4 endColor;
};

struct ParticleTrailPointGPU {
	float4 positionAge;
	float4 color;
	float4 emissive;
	uint birthLow;
	uint birthHigh;
	float distance;
	float reserved;
};

struct ParticleTrailGPU {
	uint ownerIndex;
	uint ownerBirthLow;
	uint ownerBirthHigh;
	uint generation;
	uint active;
	uint begin;
	uint count;
	uint reserved;
	ParticleTrailPointGPU head;
};

ParticleTrailPointGPU MakeParticleTrailPoint(ParticleAttributesGPU particle, ParticleParentGPU parent) {
	ParticleTrailPointGPU trailPoint = (ParticleTrailPointGPU) 0;
	trailPoint.positionAge.xyz = ParticleWorldPosition(particle, parent);
	trailPoint.color = particle.color;
	trailPoint.emissive = particle.emissive;
	trailPoint.birthLow = particle.birthLow;
	trailPoint.birthHigh = particle.birthHigh;
	trailPoint.reserved = particle.uvRotationPivotAlpha.w;
	return trailPoint;
}

#endif
