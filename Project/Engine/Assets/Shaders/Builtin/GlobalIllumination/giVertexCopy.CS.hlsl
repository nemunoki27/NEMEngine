#include "../Mesh/Common/meshShaderSharedTypes.hlsli"

cbuffer GIVertexConstants : register(b0, space7) {

	uint sourceDescriptor;
	uint sourceOffset;
	uint vertexCount;
	uint activeSubMesh;
};
RWStructuredBuffer<MeshVertex> gGIVertices : register(u0, space7);

[numthreads(64, 1, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {

	uint index = dispatchID.x;
	if (index >= vertexCount) return;
	StructuredBuffer<MeshVertex> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(sourceDescriptor)];
	gGIVertices[index] = vertices[sourceOffset + index];
}
