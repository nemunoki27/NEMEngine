//============================================================================
//	include
//============================================================================
#include "../../Common/particle.hlsli"

//============================================================================
//	resources
//============================================================================
cbuffer ParticleShapeConstants : register(b1) {

	uint divide;
	uint uvMode;
	uint2 _pad;
};

uint GetParticleShapeTriangleCount() {
	return divide * 2u;
}

void BuildParticleRingTriangle(uint triangleIndex, uint particleIndex, out VSOutput outputs[3]) {

	ParticleGeometryData instance = gParticleGeometry[particleIndex];
	// 粒子ごとの形状パラメータ、x=外周半径 y=内周半径 z=開始角 w=終了角
	const float outer = instance.shapeParams0.x;
	const float inner = instance.shapeParams0.y;
	const float startAngle = instance.shapeParams0.z;
	const float angleStep = (instance.shapeParams0.w - instance.shapeParams0.z) / (float)divide;

	const uint segment = triangleIndex / 2u;
	const uint half = triangleIndex % 2u;

	// セグメントの外周内周4頂点から三角形2枚を張る
	uint cornerSegments[3];
	uint cornerInners[3];
	if (half == 0u) {
		cornerSegments[0] = segment; cornerInners[0] = 0u;
		cornerSegments[1] = segment + 1u; cornerInners[1] = 0u;
		cornerSegments[2] = segment; cornerInners[2] = 1u;
	} else {
		cornerSegments[0] = segment; cornerInners[0] = 1u;
		cornerSegments[1] = segment + 1u; cornerInners[1] = 0u;
		cornerSegments[2] = segment + 1u; cornerInners[2] = 1u;
	}

	for (uint k = 0; k < 3u; ++k) {

		const float angle = startAngle + angleStep * (float)cornerSegments[k];
		const float radius = cornerInners[k] != 0u ? inner : outer;
		const float3 localPos = float3(cos(angle) * radius, sin(angle) * radius, 0.0f);
		const float2 uv = float2((float)cornerSegments[k] / (float)divide, cornerInners[k] != 0u ? 1.0f : 0.0f);

		VSOutput output;
		float4 worldPos = mul(float4(localPos, 1.0f), instance.worldMatrix);
		output.position = mul(worldPos, viewProjection);
		output.texcoord = uv;
		output.vertexColor = instance.vertexColor;
		output.particleIndex = particleIndex;
		outputs[k] = output;
	}
}
