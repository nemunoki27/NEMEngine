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
	uint shapeStateSlot;
	uint moduleStateCount;
	uint geometryShape;
};

StructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(t0);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t1);
StructuredBuffer<float4> gGPUModuleStatesRead : register(t15);

float3 ParticleEulerAngles(float4 q) {
	return float3(atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)),
		asin(clamp(2 * (q.w * q.y - q.z * q.x), -1, 1)),
		atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)));
}

// 粒子の描画とグラフ入力で共通の配置を求める
void ResolveParticleGPUWorldTransform(ParticleAttributesGPU particle, ParticleParentGPU parent,
	out float3 position, out float4 rotation, out float3 scale) {
	position = ParticleWorldPosition(particle, parent);
	rotation = particle.rotation;
	scale = particle.scaleSize.xyz * particle.scaleSize.w;
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
}

#endif
