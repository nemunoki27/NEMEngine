//============================================================================
//	include
//============================================================================
#include "../Common/defaultMesh.hlsli"

//============================================================================
//	main
//============================================================================
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {

	MeshVertex vertex = LoadMeshVertex(instanceID, vertexID);

	// 頂点が属するサブメッシュのローカル行列を親行列に掛ける
	uint localSubMeshIndex = gVertexSubMeshIndices[vertexID];
	float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);
	float4x4 normalMatrix = GetInstanceSubMeshNormalMatrix(instanceID, localSubMeshIndex);
	float4 worldPos = mul(vertex.position, worldMatrix);

	VSOutput output;

	output.position = mul(worldPos, viewProjection);
	output.worldPos = worldPos.xyz;
	// 法線はnormalMatrix、接線は位置と同じworldMatrixで変換する
	output.normal = TransformMeshNormalToWorld(vertex.normal, normalMatrix);
	output.tangent = TransformMeshTangentToWorld(vertex.tangent, worldMatrix);
	output.uv = vertex.uv;
	output.instanceID = instanceID;
	output.subMeshIndex = localSubMeshIndex;
	output.tangentSign = vertex.tangentSign;
	output.orientationSign = GetInstanceSubMeshOrientationSign(instanceID, localSubMeshIndex);

	return output;
}