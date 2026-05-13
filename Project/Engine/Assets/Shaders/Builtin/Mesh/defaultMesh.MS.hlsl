//============================================================================
//	include
//============================================================================

#include "defaultMesh.hlsli"

//============================================================================
//	main
//============================================================================
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void main(uint groupThreadID : SV_GroupThreadID, in payload MeshDispatchPayload payload,
	out vertices VSOutput outVerts[64], out indices uint3 outTris[124]) {

	uint meshletIndex = payload.meshletIndex;
	uint instanceIndex = payload.instanceIndex;

	MeshletDesc meshlet = gMeshlets[meshletIndex];
	SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

	if (groupThreadID < meshlet.primitiveCount) {

		outTris[groupThreadID] = UnpackPrimitiveIndex(gMeshletPrimitiveIndices[meshlet.primitiveOffset + groupThreadID]);
	}

	if (groupThreadID < meshlet.vertexCount) {

		uint vertexIndex = gMeshletVertexIndices[meshlet.vertexOffset + groupThreadID];
		MeshVertex vertex = LoadMeshVertex(instanceIndex, vertexIndex);

		// 頂点が属するサブメッシュのローカル行列を親行列に掛ける
		uint localSubMeshIndex = meshlet.subMeshIndex;
		float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceIndex, localSubMeshIndex);
		float4 worldPos = mul(vertex.position, worldMatrix);

		VSOutput output;
		output.position = mul(worldPos, viewProjection);
		output.worldPos = worldPos.xyz;
		output.normal = normalize(mul(vertex.normal, (float3x3) worldMatrix));
		output.uv = vertex.uv;
		output.instanceID = instanceIndex;
		output.subMeshIndex = localSubMeshIndex;

		outVerts[groupThreadID] = output;
	}
}
