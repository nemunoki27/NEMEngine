//============================================================================
//	include
//============================================================================
#include "../../Mesh/Common/deferredGBuffer.hlsli"
#include "../raytracingSceneData.hlsli"
#include "../../Common/hexTiling.hlsli"

cbuffer PostProcessFrameConstants : register(b0) {

	float2 resolution;
	float2 invResolution;
	float time;
	float deltaTime;
	uint frameIndex;
	float _framePad0;
	float cameraNear;
	float cameraFar;
	float2 _framePad1;
	float3 cameraWorldPos;
	float _framePad2;
	float4x4 cameraView;
	float4x4 cameraViewInverse;
	float4x4 cameraProjection;
	float4x4 cameraProjectionInverse;
};

cbuffer PostProcessParameters : register(b1) {

	float radius;
	uint sampleCount;
	uint cullingMask;
	uint opaqueGeometryAllowed;
};

RaytracingAccelerationStructure gSceneTLAS : register(t0);
Texture2D<float4> gSourcePosition : register(t1);
Texture2D<float4> gSourceNormal : register(t2);
Texture2D<uint> gSourceFlags : register(t3);
StructuredBuffer<RaytracingInstanceShaderData> gRaytracingSceneInstances : register(t4);
StructuredBuffer<RaytracingGeometryShaderData> gRaytracingGeometries : register(t5);
StructuredBuffer<SubMeshShaderData> gRaytracingSubMeshes : register(t6);
SamplerState gSampler : register(s0);
RWTexture2D<float> gDestColor : register(u0);

static const float2 kSampleAzimuth[8] = {
	float2(1.000000000f, 0.000000000f),
	float2(-0.737368873f, -0.675490300f),
	float2(0.087425709f, 0.996171042f),
	float2(0.608438880f, -0.793600737f),
	float2(-0.984713491f, 0.174181919f),
	float2(0.843755274f, 0.536728086f),
	float2(-0.259604259f, -0.965715087f),
	float2(-0.460907074f, 0.887448404f)
};

uint HashAO(uint value) {

	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

bool AcceptAOHit(inout RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query) {

	RaytracingInstanceShaderData instance = gRaytracingSceneInstances[query.CandidateInstanceID()];
	if (((instance.renderFlags >> 8u) & cullingMask) == 0u) return false;
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + query.CandidateGeometryIndex()];
	SubMeshShaderData material = gRaytracingSubMeshes[geometry.subMeshDataIndex];
	uint mode = material._materialPad >> 16;
	if (mode == 3u) return false;
	if (mode != 2u) return true;
	StructuredBuffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.indexDescriptorIndex)];
	StructuredBuffer<MeshVertex> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexDescriptorIndex)];
	uint triangleOffset = geometry.indexOffset + query.CandidatePrimitiveIndex() * 3u;
	float2 bary = query.CandidateTriangleBarycentrics();
	float2 uv = vertices[instance.vertexOffset + indices[triangleOffset]].uv * (1.0f - bary.x - bary.y) +
		vertices[instance.vertexOffset + indices[triangleOffset + 1u]].uv * bary.x +
		vertices[instance.vertexOffset + indices[triangleOffset + 2u]].uv * bary.y;
	uv = mul(float4(uv, 0.0f, 1.0f), material.uvMatrix).xy;
	HexTilingData hex = BuildHexTiling(uv, material.hexTiling);
	float alpha = material.importedBaseColor.a * material.color.a;
	if (material.baseColorTextureIndex != 0xffffffffu) {
		Texture2D<float4> color = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorTextureIndex)];
		alpha *= SampleHexLevel(color, gSampler, hex, 0.0f, false).a;
	}
	if (material.opacityTextureIndex != 0xffffffffu) {
		Texture2D<float4> opacity = ResourceDescriptorHeap[NonUniformResourceIndex(material.opacityTextureIndex)];
		alpha *= SampleHexLevel(opacity, gSampler, hex, 0.0f, false).r;
	}
	// 切り抜き部分を遮蔽物として扱わない
	return alpha >= float(material._materialPad & 65535u) / 65535.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {

	uint2 pixel = dispatchID.xy;
	if (any(pixel >= uint2(resolution))) return;
	uint width, height;
	gSourcePosition.GetDimensions(width, height);
	uint2 source = min(uint2((float2(pixel) + 0.5f) * invResolution * float2(width, height)), uint2(width, height) - 1u);
	if ((gSourceFlags.Load(int3(source, 0)) & kMaterialFlagSurface) == 0u) {
		gDestColor[pixel] = 1.0f;
		return;
	}
	float3 position = gSourcePosition.Load(int3(source, 0)).xyz;
	float3 normal = normalize(gSourceNormal.Load(int3(source, 0)).xyz * 2.0f - 1.0f);
	float3 tangent = normalize(cross(abs(normal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), normal));
	float3 bitangent = cross(normal, tangent);
	uint count = clamp(sampleCount, 1u, 8u);
	float rotation = float(HashAO(source.x + source.y * width + frameIndex * 747796405u)) / 4294967296.0f;
	float sine, cosine;
	sincos(6.2831853f * rotation, sine, cosine);
	float3 rotatedTangent = tangent * cosine + bitangent * sine;
	float3 rotatedBitangent = bitangent * cosine - tangent * sine;
	float visibility = 0.0f;
	float distanceLimit = max(radius, 0.001f);
	RayDesc ray;
	ray.Origin = position + normal * max(distanceLimit * 0.001f, 0.0001f);
	ray.TMin = 0.0001f;
	ray.TMax = distanceLimit;
	[loop]
	for (uint index = 0u; index < count; ++index) {

		float r = sqrt((float(index) + 0.5f) / float(count));
		ray.Direction = r * (rotatedTangent * kSampleAzimuth[index].x + rotatedBitangent * kSampleAzimuth[index].y) +
			normal * sqrt(1.0f - r * r);
		RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
		// 常時判定マスクで影と反射の設定から独立させる
		query.TraceRayInline(gSceneTLAS, opaqueGeometryAllowed != 0u ? 0u : RAY_FLAG_FORCE_NON_OPAQUE, 4u, ray);
		while (query.Proceed()) {
			if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && AcceptAOHit(query)) {
				query.CommitNonOpaqueTriangleHit();
			}
		}
		visibility += query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ?
			saturate(query.CommittedRayT() / distanceLimit) : 1.0f;
	}
	visibility /= float(count);
	gDestColor[pixel] = visibility;
}
