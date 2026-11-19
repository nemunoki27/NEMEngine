#include "particleGPUView.hlsli"
#include "particleGPUDraw.hlsli"
#include "../../Mesh/Common/meshShaderSharedTypes.hlsli"
#include "particleGPUShape.hlsli"

StructuredBuffer<MeshVertex> gVertices : register(t2);
StructuredBuffer<uint2> gGPUViewKeys : register(t3);
StructuredBuffer<uint> gGPUViewCounts : register(t4);

ParticleGPUVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
	uint particleIndex = gGPUViewKeys[gGPUViewCounts[phaseCount + phaseIndex] + instanceID].y;
	ParticleAttributesGPU particle = gGPUParticles[particleIndex];
	ParticleParentGPU parent = gGPUParents[particle.phase];
	float3 position;
	float4 rotation;
	float3 scale;
	ResolveParticleGPUWorldTransform(particle, parent, position, rotation, scale);
	MeshVertex vertex = gVertices[vertexID];
	float4 shapeColor = ParticleGPUShapeVertex(vertex, particleIndex);
	float3 world = ParticleRotate(vertex.position.xyz * scale, rotation) + position;
	ParticleGPUVertexOutput output;
	output.geometryIndex = particleIndex;
	output.position = mul(float4(world, 1), viewProjection);
	float2 uv = vertex.uv;
	if (flipV != 0) uv.y = 1 - uv.y;
	float2 pivot = particle.uvRotationPivotAlpha.yz;
	uv = (uv - pivot) * particle.uv.zw;
	float angle = radians(particle.uvRotationPivotAlpha.x);
	output.uv = float2(uv.x * cos(angle) - uv.y * sin(angle), uv.x * sin(angle) + uv.y * cos(angle)) + pivot + particle.uv.xy;
	output.color = particle.color * shapeColor;
	output.emissive = particle.emissive;
	output.alphaReference = particle.uvRotationPivotAlpha.w;
	output.blendMode = blendMode;
	output.particleColor = particle.color;
	output.worldPosition = world;
	float3 safeScale = float3(abs(scale.x) > 0.00001f ? scale.x : 1,
		abs(scale.y) > 0.00001f ? scale.y : 1, abs(scale.z) > 0.00001f ? scale.z : 1);
	output.normal = ParticleNormalize(ParticleRotate(vertex.normal / safeScale, rotation), float3(0, 0, -1));
	output.tangent = float4(ParticleNormalize(ParticleRotate(vertex.tangent.xyz * scale, rotation), float3(1, 0, 0)),
		vertex.tangentSign * (scale.x * scale.y * scale.z < 0 ? -1 : 1));
	float3 velocity = particle.hasParent != 0 ? mul(float4(particle.velocityLifetime.xyz, 0), parent.matrix).xyz :
		particle.velocityLifetime.xyz;
	output.velocityAge = float4(velocity, particle.positionAge.w);
	output.lifetimeRandom = float2(particle.velocityLifetime.w,
		float(ParticleHash(particle.birthLow ^ ParticleHash(particle.birthHigh)) >> 8) * (1.0f / 16777216.0f));
	return output;
}
