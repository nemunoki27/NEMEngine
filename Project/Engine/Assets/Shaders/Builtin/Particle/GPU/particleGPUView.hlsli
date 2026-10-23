#ifndef NEM_PARTICLE_GPU_VIEW
#define NEM_PARTICLE_GPU_VIEW
#include "particleGPUShared.hlsli"

cbuffer ParticleGPUViewConstants : register(b0) {
	float4x4 viewProjection;
	float4 cameraPosition;
	float4 cameraForward;
	float4 frustum[6];
	uint capacity;
	uint phaseCount;
	uint indexCount;
	uint operation;
	uint radixShift;
	uint groupCount;
	uint sortEnabled;
	uint phaseIndex;
	uint billboardMask;
	uint is2D;
	uint blendMode;
	uint flipV;
	float geometryRadius;
	float3 viewPadding;
};

StructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(t0);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t1);

#endif
