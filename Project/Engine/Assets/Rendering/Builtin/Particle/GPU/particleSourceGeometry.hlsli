#ifndef NEM_PARTICLE_SOURCE_GEOMETRY
#define NEM_PARTICLE_SOURCE_GEOMETRY

struct ParticleSourceSubMeshGPU {
	float4x4 world;
	float4x4 normal;
	float4x4 previousWorld;
	uint enabled;
	uint3 reserved;
};

#endif
