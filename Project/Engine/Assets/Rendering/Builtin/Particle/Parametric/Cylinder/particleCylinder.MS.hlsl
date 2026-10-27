#include "particleCylinderGeometry.hlsli"

#define PARTICLE_GROUP_TRIANGLES 64

[numthreads(PARTICLE_GROUP_TRIANGLES, 1, 1)]
[outputtopology("triangle")]
void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID,
	out vertices VSOutput verts[PARTICLE_GROUP_TRIANGLES * 3],
	out indices uint3 tris[PARTICLE_GROUP_TRIANGLES]) {

	const uint totalTriangles = GetParticleShapeTriangleCount();
	const uint triangleBase = groupID.x * PARTICLE_GROUP_TRIANGLES;
	// 範囲外のグループは出力数を0にする
	const uint triangleCount = triangleBase < totalTriangles ?
		min((uint)PARTICLE_GROUP_TRIANGLES, totalTriangles - triangleBase) : 0u;
	SetMeshOutputCounts(triangleCount * 3u, triangleCount);
	if (groupThreadID >= triangleCount) return;

	VSOutput triangleVertices[3];
	BuildParticleCylinderTriangle(triangleBase + groupThreadID, groupID.y, triangleVertices);
	for (uint k = 0; k < 3u; ++k) {
		verts[groupThreadID * 3u + k] = triangleVertices[k];
	}
	tris[groupThreadID] = uint3(groupThreadID * 3u, groupThreadID * 3u + 1u, groupThreadID * 3u + 2u);
}