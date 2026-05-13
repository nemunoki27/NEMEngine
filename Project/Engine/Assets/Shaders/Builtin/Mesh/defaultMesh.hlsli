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
struct DepthVSOutput {

	float4 position : SV_Position;
};

//============================================================================
//	resources
//============================================================================

cbuffer ViewConstants : register(b0) {
	
	float4x4 viewProjection;
	float4x4 cullingViewProjection;
	float3 cullingCameraPos;
	float _viewPad0;
	float2 viewSize;
	float2 cullingViewSize;
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
	uint cullingEnabled;
	uint packedMeshletVertexIndices;
	uint occlusionCullingEnabled;
	uint contributionCullingEnabled;
	uint normalConeCullingEnabled;
	float3 meshBoundsCenter;
	float meshBoundsRadius;
	float contributionPixelThreshold;
	uint3 _meshDrawPad0;
};
cbuffer HZBConstants : register(b2, space1) {

	uint2 hzbSize;
	uint hzbMipCount;
	uint hzbOcclusionEnabled;
	float hzbOcclusionBias;
	float3 _hzbPad0;
};
struct MeshVertex {

	float3 normal;
	float2 uv;
	float4 position;
};
struct MeshPackedVertex {

	uint normalOct;
	float2 uv;
	float4 position;
};
struct MeshletDrawDesc {

	uint vertexOffset;
	uint vertexCount;
	uint primitiveOffset;
	uint primitiveCount;

	uint subMeshIndex;
};
struct MeshletBounds {

	float3 center;
	float radius;
	float3 coneAxis;
	float coneCutoff;
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

struct MeshDispatchPayload {

	uint meshletIndices[32];
	uint instanceIndices[32];
};

StructuredBuffer<MeshPackedVertex> gPackedVertices : register(t0);
StructuredBuffer<uint> gIndices : register(t1);
StructuredBuffer<uint> gVertexSubMeshIndices : register(t9);
StructuredBuffer<MeshInstance> gMeshInstances : register(t2);
StructuredBuffer<MeshVertex> gSkinnedVertices : register(t4, space1);
StructuredBuffer<MeshPackedVertex> gSkinnedPackedVertices : register(t6, space1);
StructuredBuffer<MeshletDrawDesc> gMeshlets : register(t0, space1);
StructuredBuffer<uint> gMeshletVertexIndices : register(t1, space1);
StructuredBuffer<uint> gMeshletPrimitiveIndices : register(t2, space1);
StructuredBuffer<SubMeshShaderData> gSubMeshes : register(t3, space1);
StructuredBuffer<MeshletBounds> gMeshletBounds : register(t4, space1);
StructuredBuffer<uint> gPackedMeshletVertexIndices : register(t5, space1);
Texture2D<float> gHZBTexture : register(t10);

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

float3 DecodeOctNormal(uint packedNormal) {

	int sx = (int)(packedNormal << 16) >> 16;
	int sy = (int)packedNormal >> 16;
	float2 f = float2(sx, sy) / 32767.0f;
	float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
	if (n.z < 0.0f) {

		float2 old = n.xy;
		n.x = (1.0f - abs(old.y)) * (old.x >= 0.0f ? 1.0f : -1.0f);
		n.y = (1.0f - abs(old.x)) * (old.y >= 0.0f ? 1.0f : -1.0f);
	}
	return normalize(n);
}

MeshVertex DecodePackedVertex(MeshPackedVertex vertex) {

	MeshVertex outVertex;
	outVertex.normal = DecodeOctNormal(vertex.normalOct);
	outVertex.uv = vertex.uv;
	outVertex.position = vertex.position;
	return outVertex;
}

uint LoadMeshletVertexIndex(uint index) {

	if (packedMeshletVertexIndices == 0u) {
		return gMeshletVertexIndices[index];
	}

	uint packedPair = gPackedMeshletVertexIndices[index >> 1];
	if ((index & 1u) == 0u) {
		return packedPair & 0xFFFFu;
	}
	return (packedPair >> 16) & 0xFFFFu;
}

MeshVertex LoadMeshVertex(uint instanceID, uint vertexIndex) {

	MeshInstance instance = gMeshInstances[instanceID];
	if ((instance.flags & MESH_INSTANCE_FLAG_SKINNED) != 0u) {
		
		return DecodePackedVertex(gSkinnedPackedVertices[instance.skinnedVertexOffset + vertexIndex]);
	}
	return DecodePackedVertex(gPackedVertices[vertexIndex]);
}

float4 GetFrustumPlane(uint index) {

	float4 col0 = float4(cullingViewProjection[0][0], cullingViewProjection[1][0], cullingViewProjection[2][0], cullingViewProjection[3][0]);
	float4 col1 = float4(cullingViewProjection[0][1], cullingViewProjection[1][1], cullingViewProjection[2][1], cullingViewProjection[3][1]);
	float4 col2 = float4(cullingViewProjection[0][2], cullingViewProjection[1][2], cullingViewProjection[2][2], cullingViewProjection[3][2]);
	float4 col3 = float4(cullingViewProjection[0][3], cullingViewProjection[1][3], cullingViewProjection[2][3], cullingViewProjection[3][3]);

	if (index == 0) { return col3 + col0; }
	if (index == 1) { return col3 - col0; }
	if (index == 2) { return col3 + col1; }
	if (index == 3) { return col3 - col1; }
	if (index == 4) { return col2; }
	return col3 - col2;
}

float4 NormalizePlane(float4 plane) {

	float len = length(plane.xyz);
	if (len <= 0.00001f) {
		return plane;
	}
	return plane / len;
}

float GetMatrixMaxScale(float4x4 inputMat) {

	float sx = length(inputMat[0].xyz);
	float sy = length(inputMat[1].xyz);
	float sz = length(inputMat[2].xyz);
	return max(sx, max(sy, sz));
}

bool IsSphereInFrustum(float3 center, float radius) {

	[unroll]
	for (uint i = 0; i < 6; ++i) {

		float4 plane = NormalizePlane(GetFrustumPlane(i));
		if (dot(plane.xyz, center) + plane.w < -radius) {
			return false;
		}
	}
	return true;
}

float CalcProjectedPixelRadius(float3 center, float radius) {

	float4 clip = mul(float4(center, 1.0f), cullingViewProjection);
	if (clip.w <= 0.00001f) {
		return contributionPixelThreshold;
	}

	float projectionScale = max(abs(cullingViewProjection[0][0]), abs(cullingViewProjection[1][1]));
	float projectedRadius = abs(radius * projectionScale / clip.w);
	return projectedRadius * max(cullingViewSize.x, cullingViewSize.y) * 0.5f;
}

bool HasContribution(float3 center, float radius) {

	if (contributionCullingEnabled == 0u) {
		return true;
	}
	return CalcProjectedPixelRadius(center, radius) >= contributionPixelThreshold;
}

bool IsOccludedByHZB(float3 center, float radius) {

	if (occlusionCullingEnabled == 0u || hzbOcclusionEnabled == 0u || hzbMipCount == 0u) {
		return false;
	}

	float3 toCamera = cullingCameraPos - center;
	float cameraDistance = length(toCamera);
	if (cameraDistance <= max(radius, 0.00001f)) {
		return false;
	}

	float4 clip = mul(float4(center, 1.0f), cullingViewProjection);
	if (clip.w <= 0.00001f) {
		return false;
	}

	float3 ndc = clip.xyz / clip.w;
	float pixelRadius = CalcProjectedPixelRadius(center, radius);
	if (pixelRadius < 2.0f) {
		return false;
	}

	float2 screenCenter = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f)) * float2(hzbSize);
	float mip = clamp(ceil(log2(max(pixelRadius * 2.0f, 1.0f))), 0.0f, (float)(hzbMipCount - 1u));
	float2 screenMin = screenCenter - pixelRadius;
	float2 screenMax = screenCenter + pixelRadius;
	if (screenMin.x < 0.0f || screenMin.y < 0.0f ||
		screenMax.x >= (float)hzbSize.x || screenMax.y >= (float)hzbSize.y) {
		return false;
	}

	float3 frontPoint = center + toCamera / cameraDistance * radius;
	float4 frontClip = mul(float4(frontPoint, 1.0f), cullingViewProjection);
	if (frontClip.w <= 0.00001f) {
		return false;
	}
	float depth = saturate(frontClip.z / frontClip.w);
	uint mipIndex = (uint)mip;
	uint2 mipSize = max(hzbSize >> mipIndex, uint2(1u, 1u));
	uint2 minTexel = clamp((uint2)(screenMin / exp2(mip)), uint2(0u, 0u), mipSize - 1u);
	uint2 maxTexel = clamp((uint2)(screenMax / exp2(mip)), uint2(0u, 0u), mipSize - 1u);

	float z0 = gHZBTexture.Load(int3(minTexel, mipIndex));
	float z1 = gHZBTexture.Load(int3(uint2(maxTexel.x, minTexel.y), mipIndex));
	float z2 = gHZBTexture.Load(int3(uint2(minTexel.x, maxTexel.y), mipIndex));
	float z3 = gHZBTexture.Load(int3(maxTexel, mipIndex));
	float hzbDepth = max(max(z0, z1), max(z2, z3));
	return depth > hzbDepth + hzbOcclusionBias;
}

bool IsNormalConeVisible(MeshletBounds bounds, float3 center, float3x3 worldMatrix) {

	if (normalConeCullingEnabled == 0u || bounds.coneCutoff < 0.5f) {
		return true;
	}

	float3 axis = normalize(mul(bounds.coneAxis, worldMatrix));
	float3 viewDir = normalize(cullingCameraPos - center);
	float coneAngleSin = sqrt(saturate(1.0f - bounds.coneCutoff * bounds.coneCutoff));
	return dot(axis, viewDir) > -coneAngleSin;
}

bool IsMeshletVisible(uint meshletIndex, uint instanceIndex) {

	if (cullingEnabled == 0u) {
		return true;
	}

	MeshletDrawDesc meshlet = gMeshlets[meshletIndex];
	float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceIndex, meshlet.subMeshIndex);
	MeshletBounds bounds = gMeshletBounds[meshletIndex];
	float3 center = mul(float4(bounds.center, 1.0f), worldMatrix).xyz;
	float radius = bounds.radius * GetMatrixMaxScale(worldMatrix);
	if (!IsSphereInFrustum(center, radius)) {
		return false;
	}
	if (!HasContribution(center, radius)) {
		return false;
	}
	if (!IsNormalConeVisible(bounds, center, (float3x3)worldMatrix)) {
		return false;
	}
	return !IsOccludedByHZB(center, radius);
}
