#include "../../Engine/Assets/Rendering/Builtin/Particle/GPU/particleGPUCollision.hlsli"

RWStructuredBuffer<float4> gWorldResults : register(u0);

[numthreads(16, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (index >= 11) return;
	ParticleGPUCollisionHit hit = ParticleEmptyCollision();
	if (index == 0) {
		ParticleGPUCollider plane = (ParticleGPUCollider) 0;
		plane.axisYExtent.y = 1;
		hit = ParticleSweepCollider(plane, float3(0, 2, 0), float3(0, -2, 0), 0.25f, 1);
	} else if (index < 5) {
		ParticleGPUCollider collider = (ParticleGPUCollider) 0;
		collider.metadata.x = index == 2 ? 2u : index == 3 ? 3u : 1u;
		collider.centerRadius.w = index == 3 ? 0.5f : 1;
		collider.axisXExtent = float4(1, 0, 0, 1);
		collider.axisYExtent = float4(0, 1, 0, 1);
		collider.axisZExtent = float4(0, 0, 1, 1);
		collider.segmentStart.y = -1;
		collider.segmentEnd.y = 1;
		if (index == 4) {
			collider.centerRadius.x = 2;
			collider.velocity.x = 2;
			hit = ParticleSweepCollider(collider, float3(1.5f, 0, 0), float3(1.5f, 0, 0), 0.25f, 1);
		} else hit = ParticleSweepCollider(collider, float3(-3, 0, 0), float3(3, 0, 0), 0.5f, 1);
	} else if (index == 5) hit = ParticleSweepHeightmap(gGPUHeightmaps[0], float3(0.2f, 2, 0.1f), float3(0.2f, -2, 0.1f), 0.25f, 1);
	else if (index == 6) hit = ParticleFindCollision(float3(-3, 0, 0), float3(3, 0, 0), 0.5f, 1, 1, 3, 0.2f, 0);
	else if (index == 7) {
		gWorldResults[index * 2] = float4(ParticleForce(float3(2, 0, 0), float3(0, 1, 0), float4(2, 4, 1, 0)), 1);
		gWorldResults[index * 2 + 1] = 0;
		return;
	} else if (index == 8) {
		gWorldResults[index * 2] = float4(ParticleSampleVectorField(gGPUVectorFields[0], 0.5f.xxx), 1);
		gWorldResults[index * 2 + 1] = 0;
		return;
	} else if (index == 9) {
		ParticleGPUSplineSample sample = ParticleSplineDistance(gGPUSplineRanges[0], 1.5f, false);
		gWorldResults[index * 2] = float4(sample.positionDistance.xyz, 1);
		gWorldResults[index * 2 + 1] = sample.tangent;
		return;
	} else if (index == 10) hit = ParticleSweepHeightmap(gGPUHeightmaps[1], float3(0.2f, 1.5f, 0.1f), float3(0.2f, 1.5f, 0.1f), 0.25f, 1, 1);
	gWorldResults[index * 2] = float4(hit.time, hit.normal);
	gWorldResults[index * 2 + 1] = float4(hit.colliderVelocity, hit.hit);
}
