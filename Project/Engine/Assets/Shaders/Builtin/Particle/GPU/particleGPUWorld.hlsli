#ifndef NEM_PARTICLE_GPU_WORLD
#define NEM_PARTICLE_GPU_WORLD
#include "particleGPUShared.hlsli"

struct ParticleGPUForceField {
	float4x4 worldToLocal;
	float4 parameters;
	float4 axis;
	uint4 metadata;
};
struct ParticleGPUVectorField {
	float4x4 worldToGrid;
	float4x4 vectorToWorld;
	uint4 grid;
	float4 parameters;
};
struct ParticleGPUSplineSample { float4 positionDistance; float4 tangent; float4 normal; };
struct ParticleGPUSplineRange { uint offset; uint count; float length; uint closed; };
struct ParticleGPUCollider {
	float4 centerRadius;
	float4 axisXExtent;
	float4 axisYExtent;
	float4 axisZExtent;
	float4 segmentEnd;
	float4 velocity;
	uint4 metadata;
	float4 segmentStart;
};
struct ParticleGPUCollisionNode { float3 minimum; uint escape; float3 maximum; uint collider; };
struct ParticleGPUHeightmap {
	float4x4 worldToLocal;
	float4x4 localToWorld;
	float4 size;
	uint4 grid;
	uint4 receiver;
	float4 velocity;
	float4 angularVelocity;
};
cbuffer ParticleGPUWorldConstants : register(b0, space3) {
	float4x4 depthViewProjection;
	float4x4 depthInverseViewProjection;
	float4 worldCollision;
	float4 depthParameters;
	uint4 worldCounts;
	uint4 worldOptions;
};
StructuredBuffer<ParticleGPUForceField> gGPUForceFields : register(t0, space3);
StructuredBuffer<ParticleGPUVectorField> gGPUVectorFields : register(t1, space3);
StructuredBuffer<float4> gGPUVectorSamples : register(t2, space3);
StructuredBuffer<ParticleGPUSplineSample> gGPUSplineSamples : register(t3, space3);
StructuredBuffer<ParticleGPUCollider> gGPUColliders : register(t4, space3);
StructuredBuffer<float> gGPUHeightSamples : register(t5, space3);
Texture2D<float> gGPUCollisionDepth : register(t6, space3);
StructuredBuffer<ParticleGPUSplineRange> gGPUSplineRanges : register(t7, space3);
StructuredBuffer<ParticleGPUHeightmap> gGPUHeightmaps : register(t8, space3);
StructuredBuffer<ParticleGPUCollisionNode> gGPUCollisionNodes : register(t9, space3);

float3 ParticleForce(float3 radial, float3 axis, float4 parameters) {
	float distance = length(radial);
	if (distance >= parameters.y) return 0;
	float attenuation = pow(saturate(1 - distance / max(parameters.y, 0.001f)), max(parameters.z, 0));
	uint type = (uint) parameters.w;
	float3 direction = type == 3u ? axis : type == 2u ?
		ParticleNormalize(cross(axis, radial), 0) : ParticleNormalize(radial, 0) * (type == 0u ? -1 : 1);
	return direction * parameters.x * attenuation;
}

void ParticleRunForceFields(inout ParticleAttributesGPU particle, ParticleGPUInstruction instruction, float deltaTime) {
	float3 acceleration = 0;
	if ((instruction.flags & 1u) != 0) {
		for (uint i = 0; i < worldCounts.x; ++i) {
			ParticleGPUForceField field = gGPUForceFields[i];
			if ((field.metadata.x & asuint(instruction.parameter3.w)) == 0) continue;
			float3 local = mul(float4(particle.positionAge.xyz, 1), field.worldToLocal).xyz;
			float3 radial = ParticleNormalize(particle.positionAge.xyz - asfloat(field.metadata.yzw), 0);
			// 距離は場のローカル境界で評価し、方向はワールド空間へ揃える
			acceleration += ParticleForce(radial * length(local), field.axis.xyz, field.parameters);
		}
		acceleration *= instruction.parameter0.x;
	} else {
		acceleration = ParticleForce(particle.positionAge.xyz - instruction.parameter1.xyz,
			ParticleNormalize(instruction.parameter2.xyz, float3(0, 1, 0)), instruction.parameter0);
	}
	particle.velocityLifetime.xyz += acceleration * deltaTime;
}

float3 ParticleReadVectorCell(ParticleGPUVectorField field, uint3 cell) {
	return gGPUVectorSamples[field.grid.w + cell.x + field.grid.x * (cell.y + field.grid.y * cell.z)].xyz;
}

float3 ParticleSampleVectorField(ParticleGPUVectorField field, float3 uvw) {
	if (any(uvw < 0) || any(uvw > 1)) return 0;
	if ((uint) field.parameters.z == 0u) return float3(0, 0, 1);
	if ((uint) field.parameters.z == 1u) return ParticleNormalize(cross(float3(0, 1, 0), uvw - 0.5f), 0);
	float3 position = saturate(uvw) * (field.grid.xyz - 1);
	uint3 low = (uint3) position;
	uint3 high = min(low + 1, field.grid.xyz - 1);
	float3 weight = frac(position);
	float3 a = lerp(ParticleReadVectorCell(field, low), ParticleReadVectorCell(field, uint3(high.x, low.yz)), weight.x);
	float3 b = lerp(ParticleReadVectorCell(field, uint3(low.x, high.y, low.z)), ParticleReadVectorCell(field, uint3(high.xy, low.z)), weight.x);
	float3 c = lerp(ParticleReadVectorCell(field, uint3(low.xy, high.z)), ParticleReadVectorCell(field, uint3(high.x, low.y, high.z)), weight.x);
	float3 d = lerp(ParticleReadVectorCell(field, uint3(low.x, high.yz)), ParticleReadVectorCell(field, high), weight.x);
	return lerp(lerp(a, b, weight.y), lerp(c, d, weight.y), weight.z);
}

void ParticleRunVectorFields(inout ParticleAttributesGPU particle, ParticleGPUInstruction instruction, float deltaTime) {
	for (uint i = 0; i < worldCounts.y; ++i) {
		ParticleGPUVectorField field = gGPUVectorFields[i];
		if ((asuint(field.parameters.w) & asuint(instruction.parameter3.w)) == 0) continue;
		float3 uvw = mul(float4(particle.positionAge.xyz, 1), field.worldToGrid).xyz;
		if (any(uvw < 0) || any(uvw > 1)) continue;
		float3 local = ParticleSampleVectorField(field, uvw);
		float3 target = ParticleNormalize(mul(float4(local, 0), field.vectorToWorld).xyz, 0) * length(local) *
			field.parameters.x * instruction.parameter0.x;
		float tightness = saturate(max(field.parameters.y, instruction.parameter0.y));
		float follow = tightness >= 1 ? 1 : 1 - pow(1 - tightness, deltaTime * 60);
		particle.velocityLifetime.xyz = lerp(particle.velocityLifetime.xyz + target * deltaTime, target, follow);
	}
}

ParticleGPUSplineSample ParticleSplineDistance(ParticleGPUSplineRange range, float distance, bool loop) {
	float value = loop && range.length > 0 ? frac(distance / range.length) * range.length : clamp(distance, 0, range.length);
	uint low = 0;
	uint high = range.count - 1;
	while (high - low > 1) {
		uint middle = (low + high) / 2;
		if (gGPUSplineSamples[range.offset + middle].positionDistance.w < value) low = middle;
		else high = middle;
	}
	ParticleGPUSplineSample a = gGPUSplineSamples[range.offset + low];
	ParticleGPUSplineSample b = gGPUSplineSamples[range.offset + high];
	float weight = saturate((value - a.positionDistance.w) / max(b.positionDistance.w - a.positionDistance.w, 1e-6f));
	a.positionDistance = lerp(a.positionDistance, b.positionDistance, weight);
	a.tangent.xyz = ParticleNormalize(lerp(a.tangent.xyz, b.tangent.xyz, weight), float3(1, 0, 0));
	a.normal.xyz = ParticleNormalize(lerp(a.normal.xyz, b.normal.xyz, weight), a.normal.xyz);
	return a;
}

float3 ParticleSplineOffset(ParticleGPUSplineSample sample, float angle, ParticleGPUInstruction instruction) {
	float3 tangent = sample.tangent.xyz;
	float3 side = ParticleNormalize(sample.normal.xyz - tangent * dot(sample.normal.xyz, tangent), float3(0, 0, 1));
	return sample.positionDistance.xyz + instruction.parameter0.z * (side * cos(angle) + cross(tangent, side) * sin(angle));
}

// 状態配列は呼出元が渡し、粒子本体の属性サイズを変えない
void ParticleRunSpline(inout ParticleAttributesGPU particle, ParticleGPUInstruction instruction, float deltaTime,
	bool initialize, inout float4 state) {
	uint binding = (uint) instruction.parameter3.x;
	if (binding >= worldOptions.w) return;
	ParticleGPUSplineRange range = gGPUSplineRanges[binding];
	if (range.count < 2 || range.length <= 1e-6f) return;
	float previous = initialize ? instruction.parameter0.y : state.x;
	float next = previous + instruction.parameter0.x * deltaTime;
	float angle = initialize ? instruction.parameter0.y * instruction.parameter0.w * 6.28318530718f : state.y;
	angle = fmod(angle + instruction.parameter0.x * deltaTime * instruction.parameter0.w * 6.28318530718f, 6.28318530718f);
	ParticleGPUSplineSample sample = ParticleSplineDistance(range, next, (instruction.flags & 1u) != 0);
	float3 position = ParticleSplineOffset(sample, angle, instruction);
	particle.positionAge.xyz = position;
	particle.velocityLifetime.xyz = 0;
	state = float4(sample.positionDistance.w, angle, 0, 0);
	if ((instruction.flags & 2u) != 0) particle.rotation = ParticleLookRotation(sample.tangent.xyz * (instruction.parameter0.x < 0 ? -1 : 1));
}

#endif
