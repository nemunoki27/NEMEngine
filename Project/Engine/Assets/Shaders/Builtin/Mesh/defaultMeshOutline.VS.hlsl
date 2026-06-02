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

	// 選択プレビューでサブメッシュ限定の場合、対象外サブメッシュの頂点は縮退させて描画しない
	if (IsSelectionSubMeshCulled(localSubMeshIndex)) {

		OutlineVertexOutput culled;
		culled.position = float4(0.0f, 0.0f, 0.0f, 0.0f);
		culled.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return culled;
	}

	float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);
	return BuildOutlineVertex(instanceID, localSubMeshIndex, vertex, worldMatrix);
}
