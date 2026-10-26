#include "../../Mesh/Common/meshShaderSharedTypes.hlsli"
#include "particleSourceGeometry.hlsli"

cbuffer ParticleMeshSamplingConstants : register(b0) {
	uint sourceVertexCount;
	uint sourceIndexOffset;
	uint sourceIndexCount;
	uint sourceKind;
	uint sourceOperation;
	uint sourceSubMeshCount;
	uint sourceElementCount;
	uint sourceGroupCountX;
};

StructuredBuffer<MeshVertex> gSourceVertices : register(t0);
StructuredBuffer<uint> gSourceIndices : register(t1);
StructuredBuffer<uint> gSourceVertexSubMeshes : register(t2);
StructuredBuffer<ParticleSourceSubMeshGPU> gSourceSubMeshes : register(t3);
RWStructuredBuffer<float> gSourceDistribution : register(u0);
RWStructuredBuffer<uint> gSourceUsedVertices : register(u1);

bool SourceWorldPosition(uint vertex, out float3 position) {
	position = 0;
	if (vertex >= sourceVertexCount) return false;
	uint subMesh = gSourceVertexSubMeshes[vertex];
	if (subMesh >= sourceSubMeshCount || gSourceSubMeshes[subMesh].enabled == 0) return false;
	position = mul(float4(gSourceVertices[vertex].position.xyz, 1), gSourceSubMeshes[subMesh].world).xyz;
	return all(isfinite(position));
}

[numthreads(256, 1, 1)]
void main(uint thread : SV_GroupIndex, uint3 group : SV_GroupID) {
	uint groupIndex = group.x + group.y * sourceGroupCountX;
	if (groupIndex >= (sourceElementCount - 1u) / 256u + 1u) return;
	uint index = groupIndex * 256u + thread;
	if (index >= sourceElementCount) return;
	if (sourceOperation == 0u) {
		gSourceUsedVertices[index] = 0;
		return;
	}
	if (sourceOperation == 1u) {
		uint vertex = gSourceIndices[sourceIndexOffset + index];
		if (vertex < sourceVertexCount) InterlockedOr(gSourceUsedVertices[vertex], 1u);
		return;
	}
	float weight = 0;
	float3 a, b, c;
	if (sourceKind == 1u) {
		if (gSourceUsedVertices[index] != 0 && SourceWorldPosition(index, a)) weight = 1;
	} else {
		uint triangleIndex = sourceKind == 2u ? index / 3u : index;
		uint edge = sourceKind == 2u ? index % 3u : 0u;
		uint base = sourceIndexOffset + triangleIndex * 3u;
		if (SourceWorldPosition(gSourceIndices[base + edge], a) &&
			SourceWorldPosition(gSourceIndices[base + (edge + 1u) % 3u], b)) {
			if (sourceKind == 2u) weight = length(b - a);
			else if (SourceWorldPosition(gSourceIndices[base + 2u], c)) weight = length(cross(b - a, c - a)) * 0.5f;
		}
	}
	gSourceDistribution[index] = isfinite(weight) ? max(weight, 0.0f) : 0.0f;
}
