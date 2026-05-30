//============================================================================
//	include
//============================================================================

#include "defaultMeshOutline.hlsli"

//============================================================================
//	main
//============================================================================
OutlineVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {

	MeshVertex vertex = LoadMeshVertex(instanceID, vertexID);
	uint localSubMeshIndex = gVertexSubMeshIndices[vertexID];
	float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);
	return BuildOutlineVertex(instanceID, localSubMeshIndex, vertex, worldMatrix);
}
