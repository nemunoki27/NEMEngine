//============================================================================
//	include
//============================================================================
#include "../../Mesh/Common/deferredGBuffer.hlsli"

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
	float _pad0;
};

struct AOInstance {

	uint vertexDescriptorIndex;
	uint indexDescriptorIndex;
	uint vertexOffset;
	uint geometryDataOffset;
	uint renderFlags;
	uint3 _pad;
};

struct AOGeometry {

	uint subMeshDataIndex;
	uint indexOffset;
	uint pickRecordIndex;
	uint _pad;
};

RaytracingAccelerationStructure gSceneTLAS : register(t0);
Texture2D<float4> gSourcePosition : register(t1);
Texture2D<float4> gSourceNormal : register(t2);
Texture2D<uint> gSourceFlags : register(t3);
StructuredBuffer<AOInstance> gRaytracingSceneInstances : register(t4);
StructuredBuffer<AOGeometry> gRaytracingGeometries : register(t5);
StructuredBuffer<SubMeshShaderData> gRaytracingSubMeshes : register(t6);
SamplerState gSampler : register(s0);
RWTexture2D<float4> gDestColor : register(u0);

uint HashAO(uint value) {

	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

bool AcceptAOHit(inout RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query) {

	AOInstance instance = gRaytracingSceneInstances[query.CandidateInstanceID()];
	if (((instance.renderFlags >> 8u) & cullingMask) == 0u) return false;
	AOGeometry geometry = gRaytracingGeometries[instance.geometryDataOffset + query.CandidateGeometryIndex()];
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
	float alpha = material.importedBaseColor.a * material.color.a;
	if (material.baseColorTextureIndex != 0xffffffffu) {
		Texture2D<float4> color = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorTextureIndex)];
		alpha *= color.SampleLevel(gSampler, uv, 0.0f).a;
	}
	if (material.opacityTextureIndex != 0xffffffffu) {
		Texture2D<float4> opacity = ResourceDescriptorHeap[NonUniformResourceIndex(material.opacityTextureIndex)];
		alpha *= opacity.SampleLevel(gSampler, uv, 0.0f).r;
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
		gDestColor[pixel] = 1.0f.xxxx;
		return;
	}
	float3 position = gSourcePosition.Load(int3(source, 0)).xyz;
	float3 normal = normalize(gSourceNormal.Load(int3(source, 0)).xyz * 2.0f - 1.0f);
	float3 tangent = normalize(cross(abs(normal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), normal));
	float3 bitangent = cross(normal, tangent);
	uint count = clamp(sampleCount, 1u, 8u);
	float rotation = float(HashAO(source.x + source.y * width + frameIndex * 747796405u)) / 4294967296.0f;
	float visibility = 0.0f;
	float distanceLimit = max(radius, 0.001f);
	[loop]
	for (uint index = 0u; index < count; ++index) {

		float r = sqrt((float(index) + 0.5f) / float(count));
		float phi = 6.2831853f * frac(float(index) * 0.61803399f + rotation);
		float3 direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(1.0f - r * r);
		RayDesc ray;
		ray.Origin = position + normal * max(distanceLimit * 0.001f, 0.0001f);
		ray.Direction = direction;
		ray.TMin = 0.0001f;
		ray.TMax = distanceLimit;
		RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
		// 常時判定マスクで影と反射の設定から独立させる
		query.TraceRayInline(gSceneTLAS, 0, 4u, ray);
		while (query.Proceed()) {
			if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && AcceptAOHit(query)) {
				query.CommitNonOpaqueTriangleHit();
			}
		}
		visibility += query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ?
			saturate(query.CommittedRayT() / distanceLimit) : 1.0f;
	}
	visibility /= float(count);
	gDestColor[pixel] = float4(visibility.xxx, 1.0f);
}
