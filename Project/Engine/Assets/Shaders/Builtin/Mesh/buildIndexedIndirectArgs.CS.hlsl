//============================================================================
//	resources
//============================================================================

cbuffer IndirectArgsConstants : register(b0) {

	uint indexCount;
	uint _pad0;
	uint _pad1;
	uint _pad2;
};
cbuffer ViewConstants : register(b1) {

	float4x4 viewProjection;
	float4x4 cullingViewProjection;
	float3 cullingCameraPos;
	float _viewPad0;
	float2 viewSize;
	float2 cullingViewSize;
};
cbuffer MeshDrawConstants : register(b2) {

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
struct MeshInstance {

	float4x4 worldMatrix;

	uint subMeshDataOffset;
	uint subMeshCount;
	uint flags;
	uint skinnedVertexOffset;
};

StructuredBuffer<MeshInstance> gMeshInstances : register(t0);
Texture2D<float> gHZBTexture : register(t10);
RWStructuredBuffer<MeshInstance> gVisibleMeshInstances : register(u0);
RWByteAddressBuffer gIndexedIndirectArgs : register(u1);

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

bool IsInstanceVisible(MeshInstance instance) {

	if (cullingEnabled == 0u) {
		return true;
	}

	float3 center = mul(float4(meshBoundsCenter, 1.0f), instance.worldMatrix).xyz;
	float radius = meshBoundsRadius * GetMatrixMaxScale(instance.worldMatrix);
	if (!IsSphereInFrustum(center, radius)) {
		return false;
	}
	if (!HasContribution(center, radius)) {
		return false;
	}
	return !IsOccludedByHZB(center, radius);
}

//============================================================================
//	main
//============================================================================
[numthreads(256, 1, 1)]
void main(uint groupThreadID : SV_GroupThreadID) {

	if (groupThreadID == 0) {

		gIndexedIndirectArgs.Store(0, indexCount);
		gIndexedIndirectArgs.Store(4, 0);
		gIndexedIndirectArgs.Store(8, 0);
		gIndexedIndirectArgs.Store(12, 0);
		gIndexedIndirectArgs.Store(16, 0);
	}
	GroupMemoryBarrierWithGroupSync();

	for (uint i = groupThreadID; i < instanceCount; i += 256u) {

		MeshInstance instance = gMeshInstances[i];
		if (!IsInstanceVisible(instance)) {
			continue;
		}

		uint visibleIndex = 0;
		gIndexedIndirectArgs.InterlockedAdd(4, 1, visibleIndex);
		gVisibleMeshInstances[visibleIndex] = instance;
	}
}
