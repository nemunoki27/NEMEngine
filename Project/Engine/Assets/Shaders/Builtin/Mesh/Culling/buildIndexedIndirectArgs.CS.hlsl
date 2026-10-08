//============================================================================
//	include
//============================================================================
#include "../Common/meshShaderSharedTypes.hlsli"

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

#include "../Common/meshViewFields.hlsli"
};
cbuffer MeshDrawConstants : register(b2) {

#include "../Common/meshDrawFields.hlsli"
};
StructuredBuffer<MeshInstance> gMeshInstances : register(t0);
StructuredBuffer<SubMeshShaderData> gSubMeshes : register(t3, space1);
Texture2D<float> gOcclusionDepthPyramid : register(t1);
RWStructuredBuffer<MeshInstance> gVisibleMeshInstances : register(u0);
RWByteAddressBuffer gIndexedIndirectArgs : register(u1);

#define NEM_OCCLUSION_DEPTH_PYRAMID gOcclusionDepthPyramid
#include "../../Common/CullingHelpers.hlsli"

void EncapsulateSphere(inout float3 center, inout float radius, float3 addCenter, float addRadius) {

	float3 diff = addCenter - center;
	float dist = length(diff);
	if (dist + addRadius <= radius) {
		return;
	}
	if (dist + radius <= addRadius) {
		center = addCenter;
		radius = addRadius;
		return;
	}

	float newRadius = (radius + dist + addRadius) * 0.5f;
	if (dist > 0.00001f) {
		center += diff * ((newRadius - radius) / dist);
	}
	radius = newRadius;
}

// 背面法アウトラインでBoundsへ加えるモデル空間方向の膨張量
float ResolveOutlineCullLocalExpansion() {
	return invertedHullOutlinePass != 0u ? outlineMaxModelExpansion : 0.0f;
}
// Camera Z Offsetによるワールド方向の追加膨張量
float ResolveOutlineCullWorldExtra() {
	return invertedHullOutlinePass != 0u ? outlineMaxAbsCameraZOffset : 0.0f;
}

void CalcInstanceCullBounds(MeshInstance instance, out float3 center, out float radius) {

	const float outlineLocal = ResolveOutlineCullLocalExpansion();
	const float outlineWorldExtra = ResolveOutlineCullWorldExtra();

	center = mul(float4(meshBoundsCenter, 1.0f), instance.worldMatrix).xyz;
	radius = (meshBoundsRadius + outlineLocal) * GetMatrixMaxScale(instance.worldMatrix) + outlineWorldExtra;
	if (instance.subMeshCount == 0u) {
		return;
	}

	bool initialized = false;
	for (uint i = 0; i < instance.subMeshCount; ++i) {

		SubMeshShaderData subMesh = gSubMeshes[instance.subMeshDataOffset + i];

		// 実描画はsubMesh.localMatrixをworldMatrixの前に掛けるため、カリングBoundsも同じ空間で膨らませる
		float3 localCenter = mul(float4(meshBoundsCenter, 1.0f), subMesh.localMatrix).xyz;
		float localRadius = (meshBoundsRadius + outlineLocal) * GetMatrixMaxScale(subMesh.localMatrix);
		float3 worldCenter = mul(float4(localCenter, 1.0f), instance.worldMatrix).xyz;
		float worldRadius = localRadius * GetMatrixMaxScale(instance.worldMatrix) + outlineWorldExtra;

		if (!initialized) {
			center = worldCenter;
			radius = worldRadius;
			initialized = true;
			continue;
		}
		EncapsulateSphere(center, radius, worldCenter, worldRadius);
	}
}

float CalcProjectedPixelRadius(float3 center, float radius) {

	float2 radiusXY = CalcProjectedPixelRadiusXY(
		cullingViewProjection, cullingView, cullingNearClip, cullingProjectionScale,
		cullingViewSize, contributionPixelThreshold, center, radius);
	return max(radiusXY.x, radiusXY.y);
}

bool HasContribution(float3 center, float radius) {

	if (contributionCullingEnabled == 0u) {
		return true;
	}
	return CalcProjectedPixelRadius(center, radius) >= contributionPixelThreshold;
}

bool IsSphereOccluded(float3 center, float radius) {

	if (occlusionCullingEnabled == 0u) {
		return false;
	}

	return IsSphereOccludedHiZ(
		cullingViewProjection, cullingView,
		cullingNearClip, cullingProjectionScale,
		cullingViewSize, cullingCameraForward,
		center, radius);
}

bool IsInstanceVisible(MeshInstance instance) {

	if (cullingEnabled == 0u) {
		return true;
	}

	float3 center;
	float radius;
	CalcInstanceCullBounds(instance, center, radius);
	if (frustumCullingEnabled != 0u &&
		!IsSphereInFrustum(cullingViewProjection, center, radius)) {
		return false;
	}
	if (!HasContribution(center, radius)) {
		return false;
	}
	if (IsSphereOccluded(center, radius)) {
		return false;
	}
	return true;
}

#include "../Common/meshLODSelection.hlsli"

void AppendVisibleInstance(MeshInstance instance) {

	MeshLODSelection selection = ResolveMeshLODSelection(instance);
	MeshInstance firstInstance = instance;
	if (selection.secondLOD != 0xFFFFFFFFu) {
		firstInstance.flags |= MESH_INSTANCE_FLAG_LOD_DITHER;
		firstInstance._motionPad.x = asuint(selection.firstCoverage);
	}
	uint firstVisibleIndex = 0;
	gIndexedIndirectArgs.InterlockedAdd(
		selection.firstLOD * 20u + 4u, 1u, firstVisibleIndex);
	gVisibleMeshInstances[
		selection.firstLOD * instanceCount + firstVisibleIndex] = firstInstance;

	if (selection.secondLOD != 0xFFFFFFFFu &&
		selection.secondCoverage > 0.0f) {

		MeshInstance secondInstance = instance;
		secondInstance.flags |= MESH_INSTANCE_FLAG_LOD_DITHER;
		secondInstance._motionPad.x = asuint(selection.secondCoverage);
		uint secondVisibleIndex = 0;
		gIndexedIndirectArgs.InterlockedAdd(
			selection.secondLOD * 20u + 4u, 1u, secondVisibleIndex);
		gVisibleMeshInstances[
			selection.secondLOD * instanceCount + secondVisibleIndex] = secondInstance;
	}
}

//============================================================================
//	main
//============================================================================
[numthreads(256, 1, 1)]
void main(uint groupThreadID : SV_GroupThreadID) {

	if (groupThreadID == 0) {

		for (uint lodIndex = 0; lodIndex < 4u; ++lodIndex) {

			const uint argsOffset = lodIndex * 20u;
			gIndexedIndirectArgs.Store(argsOffset + 0u, lodIndexCounts[lodIndex]);
			gIndexedIndirectArgs.Store(argsOffset + 4u, 0u);
			gIndexedIndirectArgs.Store(argsOffset + 8u, lodIndexOffsets[lodIndex]);
			gIndexedIndirectArgs.Store(argsOffset + 12u, 0u);
			// SV_InstanceIDにはStartInstanceLocationが加算されないため常に0
			gIndexedIndirectArgs.Store(argsOffset + 16u, 0u);
		}
	}
	// UAVの初期化を全スレッドへ反映してから加算する
	DeviceMemoryBarrierWithGroupSync();
	if (preserveInstanceOrder != 0u) {
		if (groupThreadID == 0u) {
			for (uint i = 0u; i < instanceCount; ++i) {
				MeshInstance instance = gMeshInstances[i];
				if (IsInstanceVisible(instance)) {
					AppendVisibleInstance(instance);
				}
			}
		}
		return;
	}

	for (uint i = groupThreadID; i < instanceCount; i += 256u) {

		MeshInstance instance = gMeshInstances[i];
		if (!IsInstanceVisible(instance)) {
			continue;
		}

		AppendVisibleInstance(instance);
	}
}
