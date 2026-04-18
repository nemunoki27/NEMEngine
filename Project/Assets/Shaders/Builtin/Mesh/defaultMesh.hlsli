//============================================================================
//	Common VS/PS
//============================================================================

//============================================================================
//	output
//============================================================================

struct VSOutput {
	
	float4 position : SV_Position;
	float3 normal : NORMAL0;
	float2 uv : TEXCOORD0;
	float3 worldPos : WORLDPOS0;
	uint instanceID : INSTANCEID0;
	uint subMeshIndex : SUBMESHINDEX0;
};

//============================================================================
//	resources
//============================================================================

cbuffer ViewConstants : register(b0) {
	
	float4x4 viewProjection;
};
cbuffer SubMeshConstants : register(b1) {

	uint indexOffset;
	uint indexCount;
	uint subMeshIndex;
	uint _pad0;
};
cbuffer MeshDrawConstants : register(b0, space1) {

	uint meshletCount;
	uint subMeshCount;
	uint instanceCount;
	uint _pad1;
};
struct MeshVertex {

	float3 normal;
	float2 uv;
	float4 position;
};
struct MeshletDesc {

	uint vertexOffset;
	uint vertexCount;
	uint primitiveOffset;
	uint primitiveCount;

	uint subMeshIndex;
	uint _pad0;
	uint _pad1;
	uint _pad2;
};
struct SubMeshShaderData {

	uint baseColorTextureIndex;
	float _pad0;
	float _pad1;
	float _pad2;

	float4x4 localMatrix;
	
	float4 importedBaseColor;
	float4 color;
	float4x4 uvMatrix;
};
struct MeshInstance {

	float4x4 worldMatrix;
	
	uint subMeshDataOffset;
	uint subMeshCount;
	uint flags;
	uint skinnedVertexOffset;
};
static const uint MESH_INSTANCE_FLAG_SKINNED = 1u;

StructuredBuffer<MeshVertex> gVertices : register(t0);
StructuredBuffer<uint> gIndices : register(t1);
StructuredBuffer<uint> gVertexSubMeshIndices : register(t9);
StructuredBuffer<MeshInstance> gMeshInstances : register(t2);
StructuredBuffer<MeshVertex> gSkinnedVertices : register(t4, space1);
StructuredBuffer<MeshletDesc> gMeshlets : register(t0, space1);
StructuredBuffer<uint> gMeshletVertexIndices : register(t1, space1);
StructuredBuffer<uint> gMeshletPrimitiveIndices : register(t2, space1);
StructuredBuffer<SubMeshShaderData> gSubMeshes : register(t3, space1);

//============================================================================
//	functions
//============================================================================

SubMeshShaderData GetInstanceSubMesh(uint instanceID, uint localSubMeshIndex) {

	MeshInstance instance = gMeshInstances[instanceID];
	uint safeCount = max(instance.subMeshCount, 1u);
	uint clampedSubMeshIndex = min(localSubMeshIndex, safeCount - 1u);

	return gSubMeshes[instance.subMeshDataOffset + clampedSubMeshIndex];
}

float4x4 GetInstanceSubMeshWorldMatrix(uint instanceID, uint localSubMeshIndex) {

	MeshInstance instance = gMeshInstances[instanceID];
	SubMeshShaderData subMesh = GetInstanceSubMesh(instanceID, localSubMeshIndex);

	return mul(subMesh.localMatrix, instance.worldMatrix);
}

uint3 UnpackPrimitiveIndex(uint packedIndex) {

	return uint3(
		packedIndex & 0x3FF,
		(packedIndex >> 10) & 0x3FF,
		(packedIndex >> 20) & 0x3FF);
}

MeshVertex LoadMeshVertex(uint instanceID, uint vertexIndex) {

	MeshInstance instance = gMeshInstances[instanceID];
	if ((instance.flags & MESH_INSTANCE_FLAG_SKINNED) != 0u) {
		
		return gSkinnedVertices[instance.skinnedVertexOffset + vertexIndex];
	}
	return gVertices[vertexIndex];
}