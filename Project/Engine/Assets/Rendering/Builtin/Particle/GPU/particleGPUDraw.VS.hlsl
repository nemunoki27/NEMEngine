#include "particleGPUView.hlsli"
#include "particleGPUDraw.hlsli"
#include "../../Mesh/Common/meshShaderSharedTypes.hlsli"
#include "particleGPUShape.hlsli"

StructuredBuffer<MeshVertex> gVertices : register(t2);
StructuredBuffer<uint2> gGPUViewKeys : register(t3);
StructuredBuffer<uint> gGPUViewCounts : register(t4);

float3 ParticleEulerAngles(float4 q) {
	return float3(atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)),
		asin(clamp(2 * (q.w * q.y - q.z * q.x), -1, 1)),
		atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)));
}

ParticleGPUVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
	uint particleIndex = gGPUViewKeys[gGPUViewCounts[phaseCount + phaseIndex] + instanceID].y;
	ParticleAttributesGPU particle = gGPUParticles[particleIndex];
	ParticleParentGPU parent = gGPUParents[particle.phase];
	float3 position = ParticleWorldPosition(particle, parent);
	float4 rotation = particle.rotation;
	float3 scale = particle.scaleSize.xyz * particle.scaleSize.w;
	if (particle.hasParent != 0) {
		rotation = normalize(ParticleQuaternionMultiply(parent.rotation, rotation));
		scale *= parent.scale.xyz;
	}
	if (is2D == 0 && billboardMask != 0) {
		float3 forward = ParticleNormalize(cameraForward.w != 0 ? -cameraForward.xyz :
			cameraPosition.xyz - position, float3(0, 0, 1));
		float4 desired = ParticleLookRotation(forward);
		if (billboardMask == 7u) rotation = normalize(ParticleQuaternionMultiply(desired, rotation));
		else {
			float3 angles = ParticleEulerAngles(rotation);
			if ((billboardMask & 1u) != 0) angles.x = atan2(-forward.y, length(forward.xz));
			if ((billboardMask & 2u) != 0) angles.y = atan2(forward.x, forward.z);
			if ((billboardMask & 4u) != 0) angles.z = ParticleEulerAngles(desired).z;
			rotation = ParticleEuler(degrees(angles));
		}
	}
	MeshVertex vertex = gVertices[vertexID];
	float4 shapeColor = ParticleGPUShapeVertex(vertex, particleIndex);
	float3 world = ParticleRotate(vertex.position.xyz * scale, rotation) + position;
	ParticleGPUVertexOutput output;
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
