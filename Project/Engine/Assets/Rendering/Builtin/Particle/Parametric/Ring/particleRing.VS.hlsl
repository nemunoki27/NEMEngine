#include "particleRingGeometry.hlsli"

// VSとMSで同じ三角形生成を使う
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {

	VSOutput triangleVertices[3];
	BuildParticleRingTriangle(vertexID / 3u, instanceID, triangleVertices);
	return triangleVertices[vertexID % 3u];
}
