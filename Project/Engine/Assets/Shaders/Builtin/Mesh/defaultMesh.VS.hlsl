//============================================================================
//	include
//============================================================================

#include "defaultMesh.hlsli"

//============================================================================
//	main
//============================================================================
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {

	MeshVertex vertex = LoadMeshVertex(instanceID, vertexID);

	// 頂点が属するサブメッシュのローカル行列を親行列に掛ける
	uint localSubMeshIndex = gVertexSubMeshIndices[vertexID];
	float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);
	float4 worldPos = mul(vertex.position, worldMatrix);

	VSOutput output;
	
	output.position = mul(worldPos, viewProjection);
	output.worldPos = worldPos.xyz;
	output.normal = normalize(mul(vertex.normal, (float3x3) worldMatrix));
	output.uv = vertex.uv;
	output.instanceID = instanceID;
	output.subMeshIndex = localSubMeshIndex;

	return output;
}