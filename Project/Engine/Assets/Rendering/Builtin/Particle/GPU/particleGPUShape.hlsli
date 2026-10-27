#ifndef NEM_PARTICLE_GPU_SHAPE
#define NEM_PARTICLE_GPU_SHAPE
#include "../Common/particleCylinderProfile.hlsli"

cbuffer ParticleGPUShapeConstants : register(b1) {
	float4 shapeBaseParams0;
	float4 shapeBaseParams1;
	float4 shapeBaseTopColor;
	float4 shapeBaseCenterColor;
	float4 shapeBaseBottomColor;
	uint shapeUVMode;
	uint3 shapePadding;
};

float4 ParticleGPUShapeVertex(inout MeshVertex vertex, uint particleIndex) {
	float4 parameters0 = shapeBaseParams0;
	float4 parameters1 = shapeBaseParams1;
	float4 topColor = shapeBaseTopColor;
	float4 centerColor = shapeBaseCenterColor;
	float4 bottomColor = shapeBaseBottomColor;
	if (shapeStateSlot != 0xffffffffu) {
		uint slot = particleIndex * moduleStateCount + shapeStateSlot;
		parameters0 = gGPUModuleStatesRead[slot];
		parameters1 = gGPUModuleStatesRead[slot + 1];
		topColor = gGPUModuleStatesRead[slot + 2];
		centerColor = gGPUModuleStatesRead[slot + 3];
		bottomColor = gGPUModuleStatesRead[slot + 4];
		// 基準形状の分割を使い、粒子ごとの半径と角度だけを変える
		if (geometryShape == 2u) {
			float angle = lerp(parameters0.z, parameters0.w, vertex.uv.x);
			float radius = vertex.uv.y > 0.5f ? parameters0.y : parameters0.x;
			vertex.position.xyz = float3(cos(angle) * radius, sin(angle) * radius, 0);
		} else if (geometryShape == 3u) {
			float heightT = saturate(vertex.position.y * 0.5f + 0.5f);
			float radial = length(vertex.position.xz);
			float angleT = atan2(max(vertex.position.z, 0.0f), vertex.position.x) / 3.14159265359f;
			float angle = angleT * parameters1.z;
			bool cap = abs(vertex.normal.y) > 0.5f;
			float radius = ParticleCylinderRadius(heightT, parameters0, parameters1);
			vertex.position.xyz = float3(cos(angle) * radius * radial, lerp(-0.5f, 0.5f, heightT) * parameters0.w,
				sin(angle) * radius * radial);
			if (cap) vertex.uv = float2(cos(angle), sin(angle)) * radial * 0.5f + 0.5f;
			else if (shapeUVMode == 1u) vertex.uv = float2(cos(angle), -sin(angle)) * heightT * 0.5f + 0.5f;
			else vertex.uv = float2(angleT, 1.0f - heightT);
			if (!cap) {
				float low = max(heightT - 0.001f, 0);
				float high = min(heightT + 0.001f, 1);
				float slope = (ParticleCylinderRadius(high, parameters0, parameters1) -
					ParticleCylinderRadius(low, parameters0, parameters1)) / max((high - low) * parameters0.w, 0.00001f);
				vertex.normal = normalize(float3(cos(angle), -slope, sin(angle)));
			}
		}
	}
	if (geometryShape != 3u || parameters1.w < 0.5f) return 1;
	float heightT = saturate(vertex.position.y / max(abs(parameters0.w), 0.00001f) + 0.5f);
	return ParticleCylinderColor(heightT, parameters1, topColor, centerColor, bottomColor);
}

#endif
