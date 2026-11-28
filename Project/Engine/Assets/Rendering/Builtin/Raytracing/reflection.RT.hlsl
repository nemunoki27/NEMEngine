#include "../Common/normalEncoding.hlsli"
//============================================================================
//	include
//============================================================================
#include "../Mesh/Common/deferredGBuffer.hlsli"
#include "raytracingSceneData.hlsli"
#include "../Primitive/primitiveUV.hlsli"
#include "../Primitive/primitiveInstanceColor.hlsli"
#include "../Common/descriptorHeapCompatibility.hlsli"
#include "../Particle/GPU/particlePaintRead.hlsli"
#include "../Particle/GPU/particleGPUShared.hlsli"
#include "../Lighting/environmentSampling.hlsli"
#include "../Lighting/lightShadowFilter.hlsli"

// RTライブラリ内で固定SRVとライトバッファが衝突しないようレジスタを差し替える
#define NEM_LIGHT_COUNTS_REGISTER b1
#define NEM_DIRECTIONAL_LIGHTS_REGISTER t11
#define NEM_POINT_LIGHTS_REGISTER t12
#define NEM_SPOT_LIGHTS_REGISTER t13
#define NEM_RECT_LIGHTS_REGISTER t14
#define NEM_LIGHTING_SAMPLER_REGISTER s0
#include "../Mesh/Common/pbrShading.hlsli"
#undef NEM_LIGHT_COUNTS_REGISTER
#undef NEM_DIRECTIONAL_LIGHTS_REGISTER
#undef NEM_POINT_LIGHTS_REGISTER
#undef NEM_SPOT_LIGHTS_REGISTER
#undef NEM_RECT_LIGHTS_REGISTER
#undef NEM_LIGHTING_SAMPLER_REGISTER

//============================================================================
//	resources
//============================================================================
cbuffer RaytracingViewConstants : register(b0) {

	float4x4 gView;
	float4x4 gProjection;
	float4x4 gInverseView;
	float4x4 gInverseProjection;
	float4x4 gInverseViewProjection;

	float3 gCameraPosition;
	float gMaxReflectionDistance;

	float2 gRenderSize;
	float2 gInvRenderSize;

	float gShadowNormalBias;
	float gReflectionIntensity;
	float gNearClip;
	float gFarClip;

	float gReflectionNormalBias;
	float gReflectionViewBias;
	float gReflectionMinHitDistance;
	float gReflectionMaxRoughness;

	float gReflectionRoughnessFade;
	float gSkyIntensity;
	float gFresnelMin;
	float gReflectionPadding;

	float4 gSkyboxColor;
	uint gSkyboxCubemapIndex;
	uint gHasSkybox;
	float gIBLIntensity;
	uint gFrameIndex;
	uint gEnvironmentSpecularIndex;
	uint gEnvironmentBRDFIndex;
	uint gPhysicalEnvironmentEnabled;
	uint gEnvironmentIrradianceIndex;
	uint gGIWorldCacheIndex;
	uint gGIWorldMomentsIndex;
	uint emitterCount;
	uint cullingMask;
	uint rayCount;
	uint gGIResolvedIndex;
	uint allLayersVisible;
	uint gGIPadding;
};

struct ReflectionPayload {

	float3 color;
	uint hit;

	float3 worldPosition;
	float hitDistance;

	float3 worldNormal;
	uint transmissionDepth;
	float3 deferredDiffuse;
};
#include "../Lighting/worldLightIndex.hlsli"

struct LightClusterHeader {

	uint offset;
	uint count;
};
cbuffer LightClusterConstants : register(b2) {

	uint clusterTileCountX;
	uint clusterTileCountY;
	uint clusterZSliceCount;
	uint clusterTileSize;

	float clusterNearClip;
	float clusterFarClip;
	float clusterSliceScale;
	float clusterSliceBias;

	uint clusterCount;
	uint clusterMaxLights;
	uint2 _clusterPad;
};

RaytracingAccelerationStructure gSceneTLAS : register(t0);
Texture2D<float4> gSourceColor : register(t1);
Texture2D<float> gSourceDepth : register(t2);
Texture2D<float4> gSourceNormal : register(t3);
Texture2D<float4> gSourcePosition : register(t4);

StructuredBuffer<RaytracingInstanceShaderData> gRaytracingSceneInstances :
	register(t5);
StructuredBuffer<SubMeshShaderData> gRaytracingSubMeshes : register(t6);
StructuredBuffer<RaytracingGeometryShaderData> gRaytracingGeometries :
	register(t7);
Texture2D<uint> gSourceFlags : register(t8);
Texture2D<float4> gSourceMaterial : register(t9);
Texture2D<float4> gSourceAlbedo : register(t10);
StructuredBuffer<LightClusterHeader> gLightClusterHeaders : register(t15);
StructuredBuffer<uint> gLightClusterIndices : register(t16);
SamplerState gEnvironmentSampler : register(s1);
#include "../Decal/decalCommon.hlsli"

// TLAS用途別マスク
static const uint kRaytracingMaskShadowCaster = 1u;
static const uint kRaytracingMaskReflectionCaster = 1u << 1;

RWTexture2D<float4> gDestColor : register(u0);
RWTexture2D<float> gReflectionHitDistance : register(u1);
RWTexture2D<float4> gReflectionHitGeometry : register(u2);

//============================================================================
//	functions
//============================================================================
float3 SafeNormalize(float3 v, float3 fallbackValue) {

	float lenSq = dot(v, v);
	if (lenSq <= 1e-8f) {
		return fallbackValue;
	}
	return v * rsqrt(lenSq);
}

// HDRの色比を保ったまま反射先のFP16に収める
float3 StoreReflectionRadiance(float3 radiance) {

	if (!all(isfinite(radiance))) return 0.0f.xxx;
	radiance = max(radiance, 0.0f.xxx);
	float peak = max(radiance.r, max(radiance.g, radiance.b));
	return radiance * (60000.0f / max(peak, 60000.0f));
}

float3 LoadPrimaryWorldPosition(uint2 pixel) {
	return gSourcePosition.Load(int3(pixel, 0)).xyz;
}

float3 DecodeWorldNormal(float3 encodedNormal) {

	float3 n = encodedNormal * 2.0f - 1.0f;
	return SafeNormalize(n, float3(0.0f, 1.0f, 0.0f));
}

float SampleWorldFootprintCandidate(
	uint2 pixel, float3 centerPos, float3 centerNormal) {

	float3 samplePos = gSourcePosition.Load(int3(pixel, 0)).xyz;
	float3 sampleNormal = DecodeWorldNormal(
		gSourceNormal.Load(int3(pixel, 0)).xyz);

	if (dot(sampleNormal, centerNormal) < 0.95f) {
		return 0.0f;
	}

	float3 delta = samplePos - centerPos;
	return dot(delta, delta);
}

float EstimatePrimaryWorldFootprint(
	uint2 pixel, uint2 dim, float3 centerPos, float3 centerNormal) {

	uint2 leftPixel = uint2((pixel.x > 0) ? pixel.x - 1 : pixel.x, pixel.y);
	uint2 rightPixel = uint2(min(pixel.x + 1, dim.x - 1), pixel.y);
	uint2 upPixel = uint2(pixel.x, (pixel.y > 0) ? pixel.y - 1 : pixel.y);
	uint2 downPixel = uint2(pixel.x, min(pixel.y + 1, dim.y - 1));

	const float maxFootprint = 0.05f;
	const float limit = maxFootprint * maxFootprint;
	float square = SampleWorldFootprintCandidate(leftPixel, centerPos, centerNormal);
	if (square >= limit) return maxFootprint;
	square = max(square, SampleWorldFootprintCandidate(rightPixel, centerPos, centerNormal));
	if (square >= limit) return maxFootprint;
	square = max(square, SampleWorldFootprintCandidate(upPixel, centerPos, centerNormal));
	if (square >= limit) return maxFootprint;
	square = max(square, SampleWorldFootprintCandidate(downPixel, centerPos, centerNormal));
	return clamp(sqrt(square), 0.0001f, maxFootprint);
}

float3 EvaluateSkyReflection(float3 direction) {

	float upT = saturate(direction.y * 0.5f + 0.5f);

	float3 horizonColor = float3(0.55f, 0.65f, 0.80f);
	float3 zenithColor = float3(0.10f, 0.22f, 0.45f);
	float3 sky = lerp(horizonColor, zenithColor, upT);

	float3 sunDir = SafeNormalize(
		float3(0.35f, 0.85f, 0.20f), float3(0.0f, 1.0f, 0.0f));
	float sunAmount = pow(saturate(dot(direction, sunDir)), 256.0f);
	sky += sunAmount * float3(1.0f, 0.95f, 0.80f) * 2.0f;

	return sky * gSkyIntensity;
}

float3 EvaluateReflectionEnvironment(float3 direction, float roughness) {

	if (gHasSkybox != 0u && gSkyboxCubemapIndex != kNoTexture) {

		uint index = gEnvironmentSpecularIndex != kNoTexture ? gEnvironmentSpecularIndex : gSkyboxCubemapIndex;
		TextureCube<float4> skybox = NEM_TEXTURECUBE(index);
		uint width, height, mipCount;
		skybox.GetDimensions(0, width, height, mipCount);
		float mip = gPhysicalEnvironmentEnabled != 0u ? roughness * max(float(mipCount) - 1.0f, 0.0f) : 0.0f;
		float3 environment = skybox.SampleLevel(gEnvironmentSampler, direction, mip).rgb * gSkyboxColor.rgb * gSkyIntensity;
		return environment;
	}
	return EvaluateSkyReflection(direction);
}

float3 EstimateWorldNormalFromPosition(
	uint2 pixel, uint2 dim) {

	uint2 leftPixel = uint2((pixel.x > 0) ? pixel.x - 1 : pixel.x, pixel.y);
	uint2 rightPixel = uint2(min(pixel.x + 1, dim.x - 1), pixel.y);
	uint2 upPixel = uint2(pixel.x, (pixel.y > 0) ? pixel.y - 1 : pixel.y);
	uint2 downPixel = uint2(pixel.x, min(pixel.y + 1, dim.y - 1));

	float3 p = LoadPrimaryWorldPosition(pixel);
	float3 pl = LoadPrimaryWorldPosition(leftPixel);
	float3 pr = LoadPrimaryWorldPosition(rightPixel);
	float3 pu = LoadPrimaryWorldPosition(upPixel);
	float3 pd = LoadPrimaryWorldPosition(downPixel);

	float3 dx = (length(pr - p) < length(p - pl)) ? (pr - p) : (p - pl);
	float3 dy = (length(pd - p) < length(p - pu)) ? (pd - p) : (p - pu);

	float3 n = SafeNormalize(cross(dy, dx), float3(0.0f, 1.0f, 0.0f));
	float3 toCamera = SafeNormalize(
		gCameraPosition - p, float3(0.0f, 0.0f, 1.0f));
	if (dot(n, toCamera) < 0.0f) {
		n = -n;
	}
	return n;
}

float3 ComputeBarycentrics(float2 bary) {

	return float3(1.0f - bary.x - bary.y, bary.x, bary.y);
}

ByteAddressBuffer gRaytracingMaterialParameters : register(t0, space34);

#include "GlobalIllumination/giGrid.hlsli"
#include "GlobalIllumination/giEmitters.hlsli"
#define ambientIntensity 0.03f
#define hasCoarseBricks 1u
#include "GlobalIllumination/giWorldRadiance.hlsli"
#undef hasCoarseBricks
#undef ambientIntensity

void ResolveRaytracingHitGeometry(
	in BuiltInTriangleIntersectionAttributes attr,
	out RaytracingInstanceShaderData instanceData,
	out SubMeshShaderData subMesh,
	out float2 uv,
	out float2 paintUV,
	out float4 vertexColor,
	out float3 worldPosition,
	out float3x3 tangentToWorld) {

	instanceData = gRaytracingSceneInstances[InstanceID()];
	RaytracingGeometryShaderData geometryData =
		gRaytracingGeometries[
			instanceData.geometryDataOffset + GeometryIndex()];
	subMesh = gRaytracingSubMeshes[geometryData.subMeshDataIndex];

	StructuredBuffer<uint> indices =
		ResourceDescriptorHeap[
			NonUniformResourceIndex(instanceData.indexDescriptorIndex)];
	StructuredBuffer<MeshVertex> vertices =
		ResourceDescriptorHeap[
			NonUniformResourceIndex(instanceData.vertexDescriptorIndex)];

	uint baseIndex = geometryData.indexOffset + PrimitiveIndex() * 3u;
	MeshVertex v0 = vertices[
		instanceData.vertexOffset + indices[baseIndex + 0u]];
	MeshVertex v1 = vertices[
		instanceData.vertexOffset + indices[baseIndex + 1u]];
	MeshVertex v2 = vertices[
		instanceData.vertexOffset + indices[baseIndex + 2u]];

	float3 bary = ComputeBarycentrics(attr.barycentrics);
	uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
	paintUV = uv;
	ResolveHitPrimitiveSurface(subMesh, v0, v1, v2, bary, paintUV, uv, vertexColor);

	float3 objectNormal = SafeNormalize(
		v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z,
		float3(0.0f, 1.0f, 0.0f));
	float3 localNormal = SafeNormalize(
		mul(float4(objectNormal, 0.0f), subMesh.localNormalMatrix).xyz,
		objectNormal);
	float3x3 worldToObject = (float3x3)WorldToObject3x4();
	float3 worldNormal = SafeNormalize(
		mul(localNormal, worldToObject), localNormal);
	float3 worldTangent = 0.0f.xxx;
	float3 worldBitangent = 0.0f.xxx;
	// 標準PBRで接線基底を使うのは解決済み法線テクスチャがある場合だけ
	[branch]
	if (subMesh.normalTextureIndex != kNoTexture) {

		float3 objectTangent = SafeNormalize(
			v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z,
			float3(1.0f, 0.0f, 0.0f));
		float tangentSign =
			v0.tangentSign * bary.x +
			v1.tangentSign * bary.y +
			v2.tangentSign * bary.z < 0.0f ? -1.0f : 1.0f;
		float3 localTangent = SafeNormalize(
			mul(float4(objectTangent, 0.0f), subMesh.localMatrix).xyz,
			objectTangent);
		float3x3 objectToWorld = (float3x3)ObjectToWorld3x4();
		worldTangent = SafeNormalize(
			mul(objectToWorld, localTangent), localTangent);
		worldTangent = OrthogonalizeTangent(worldTangent, worldNormal);

		float worldOrientationSign = determinant(objectToWorld) < 0.0f ? -1.0f : 1.0f;
		float bitangentSign = tangentSign *
			subMesh.localOrientationSign * worldOrientationSign;
		worldBitangent = SafeNormalize(cross(worldNormal, worldTangent),
			float3(0.0f, 0.0f, 1.0f)) * bitangentSign;
	}

	// 両面形状の裏面ヒットでも法線を入射側へ向ける
	if (dot(worldNormal, WorldRayDirection()) > 0.0f) {
		worldNormal = -worldNormal;
		worldBitangent = -worldBitangent;
	}
	tangentToWorld = float3x3(
		worldTangent, worldBitangent, worldNormal);
	worldPosition = WorldRayOrigin() +
		WorldRayDirection() * RayTCurrent();
}

#ifndef NEM_REFLECTION_CUSTOM_HIT
bool LoadRasterHitMaterial(float3 worldPosition, inout ResolvedPBRMaterial material, out int2 rasterPixel);

ResolvedPBRMaterial ResolveRaytracingHitMaterial(
	in BuiltInTriangleIntersectionAttributes attr,
	out RaytracingInstanceShaderData instanceData,
	out float3 worldPosition, out SubMeshShaderData subMesh, out int2 rasterPixel) {

	float2 uv;
	float2 paintUV;
	float4 vertexColor;
	float3x3 tangentToWorld;
	ResolveRaytracingHitGeometry(attr, instanceData, subMesh,
		uv, paintUV, vertexColor, worldPosition, tangentToWorld);
	// レイ距離に応じたMipを使い遠距離ヒットの高周波ノイズを抑える
	HexTilingData hex = BuildHexTiling(uv, subMesh.hexTiling);
	float textureMip = max(log2(1.0f + RayTCurrent() * 0.02f), 0.0f);

	ResolvedPBRMaterial material = (ResolvedPBRMaterial)0;
	material.N = tangentToWorld[2];
	if (subMesh.normalTextureIndex != kNoTexture) {

		float3 tangentNormal = SampleHitTexture(
			subMesh.normalTextureIndex, hex, textureMip, 1.0f.xxxx, true).xyz * 2.0f - 1.0f;
		material.N = SafeNormalize(
			mul(tangentNormal, tangentToWorld), material.N);
	}

	rasterPixel = int2(-1, -1);
	bool painted = HasMaterialPaint(instanceData.renderFlags);
	if (subMesh.useRasterHitMaterial != 0u && !painted) {
		if (LoadRasterHitMaterial(worldPosition, material, rasterPixel)) return material;
	}

	float4 baseColor = subMesh.importedBaseColor * subMesh.color * vertexColor;
	baseColor *= SampleHitTexture(
		subMesh.baseColorTextureIndex, hex, textureMip, 1.0f.xxxx);
	baseColor.a *= SampleHitTexture(
		subMesh.opacityTextureIndex, hex, textureMip, 1.0f.xxxx).r;
	ApplyParticlePaint(uint2(instanceData.entityIndex, instanceData.entityGeneration), paintUV,
		worldPosition, gEnvironmentSampler, baseColor);

	// glTFのmetallic-roughness規約に合わせてBとGを参照する
	float4 metallicRoughness = SampleHitTexture(
		subMesh.metallicRoughnessTextureIndex, hex, textureMip, 1.0f.xxxx);
	float metallicSample = SampleHitTexture(
		subMesh.metallicTextureIndex, hex, textureMip, 1.0f.xxxx).r;
	float roughnessSample = SampleHitTexture(
		subMesh.roughnessTextureIndex, hex, textureMip, 1.0f.xxxx).r;
	float metallic = saturate(
		subMesh.metallic * metallicRoughness.b * metallicSample);
	float roughness = max(
		saturate(subMesh.roughness * metallicRoughness.g * roughnessSample),
		0.04f);
	float ao = SampleHitTexture(
		subMesh.occlusionTextureIndex, hex, textureMip, 1.0f.xxxx).r;

	float3 emissive = subMesh.emissiveColor.rgb *
		subMesh.emissiveColor.a;
	emissive *= SampleHitTexture(
		subMesh.emissiveTextureIndex, hex, textureMip, 1.0f.xxxx).rgb;

	material.baseColor = baseColor;
	material.metallic = metallic;
	material.roughness = roughness;
	material.ao = ao;
	material.emissive = emissive;
	if (painted) {
		RaytracingGeometryShaderData geometry = gRaytracingGeometries[instanceData.geometryDataOffset+GeometryIndex()];
		PaintFootprint f = {0.0f.xxx,0.0f.xxx,textureMip};
		ApplyMaterialPaint(uint2(instanceData.entityIndex,instanceData.entityGeneration),PrimitiveIndex(),geometry.indexOffset/3,
			worldPosition,tangentToWorld[2],uv,0,0,f,gSampler,material.baseColor.rgb,material.N,
			material.roughness,material.metallic,material.ao,material.emissive);
		if (subMesh.useRasterHitMaterial != 0u) LoadRasterHitMaterial(worldPosition, material, rasterPixel);
	}
	return material;
}

#endif

bool ProjectWorldToPixel(
	float3 worldPosition, out int2 pixel, out float viewDepth) {

	float4 viewPosition = mul(float4(worldPosition, 1.0f), gView);
	float4 clipPosition = mul(viewPosition, gProjection);
	viewDepth = viewPosition.z;
	pixel = int2(0, 0);
	if (clipPosition.w <= 1e-5f) {
		return false;
	}

	float3 ndc = clipPosition.xyz / clipPosition.w;
	if (ndc.x < -1.0f || 1.0f < ndc.x ||
		ndc.y < -1.0f || 1.0f < ndc.y ||
		ndc.z < 0.0f || 1.0f < ndc.z) {

		return false;
	}

	float2 screenUV = float2(
		ndc.x * 0.5f + 0.5f,
		-ndc.y * 0.5f + 0.5f);
	pixel = int2(min(
		uint2(screenUV * gRenderSize),
		uint2(gRenderSize) - 1u));
	return true;
}


bool ResolveRaytracingLightCluster(
	int2 pixel, float viewDepth, out LightClusterHeader header) {

	header.offset = 0u;
	header.count = 0u;
	if (clusterCount == 0u || clusterTileCountX == 0u ||
		clusterTileCountY == 0u || clusterZSliceCount == 0u) {

		return false;
	}

	if (pixel.x < 0) return false;
	uint2 tile = min(
		uint2(max(pixel, int2(0, 0))) / max(clusterTileSize, 1u),
		uint2(clusterTileCountX - 1u, clusterTileCountY - 1u));
	uint zSlice = (uint)clamp(
		floor(log2(max(viewDepth, clusterNearClip)) *
			clusterSliceScale + clusterSliceBias),
		0.0f, float(clusterZSliceCount - 1u));
	uint clusterIndex =
		(zSlice * clusterTileCountY + tile.y) *
		clusterTileCountX + tile.x;
	if (clusterCount <= clusterIndex) {
		return false;
	}

	header = gLightClusterHeaders[clusterIndex];
	return header.count <= 24u;
}

float ResolveRaytracingShadow(float3 worldPosition, float3 worldNormal,
	float3 direction, float maxDistance, float shadowStrength,
	uint renderFlags, uint filterIndex) {

	if (shadowStrength <= 0.0f ||
		(renderFlags & MESH_INSTANCE_FLAG_RECEIVE_SHADOW) == 0u ||
		maxDistance <= 0.002f) {

		return 1.0f;
	}

	RayDesc ray;
	ray.Origin = worldPosition + worldNormal *
		max(gShadowNormalBias, 0.0001f);
	ray.Direction = SafeNormalize(direction, worldNormal);
	ray.TMin = 0.001f;
	ray.TMax = max(maxDistance - 0.001f, ray.TMin);
	if (HasLightShadowExclusions(filterIndex)) {
		float hitDistance;
		return TraceFilteredLightShadow(gSceneTLAS, ray, kRaytracingMaskShadowCaster, filterIndex, hitDistance) ?
			1.0f - saturate(shadowStrength) : 1.0f;
	}

	RayQuery <
		RAY_FLAG_FORCE_OPAQUE |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > rayQuery;
	rayQuery.TraceRayInline(
		gSceneTLAS, 0, kRaytracingMaskShadowCaster, ray);
	while (rayQuery.Proceed()) {
	}
	return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT ?
		1.0f - saturate(shadowStrength) : 1.0f;
}

float3 EvaluateRaytracingDirectionalLight(DirectionalLight light,
	float3 worldPosition, ResolvedPBRMaterial material,
	float3 V, float3 F0, uint renderFlags) {

	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, renderFlags >> 8u)) return 0.0f.xxx;

	float3 L = SafeNormalize(-light.direction, material.N);
	if (dot(material.N, L) <= 0.0f || !any(light.color.rgb * light.intensity != 0.0f)) {
		return 0.0f.xxx;
	}
	float shadow = ResolveRaytracingShadow(
		worldPosition, material.N, L,
		max(gFarClip, gMaxReflectionDistance),
		light.shadowStrength, renderFlags, light.shadowFilterIndex);
	if (shadow == 0.0f) return 0;
	return shadow * EvaluatePBRDirectionalLight(
		light, material.N, V, material.baseColor.rgb,
		material.metallic, material.roughness, F0);
}

float3 EvaluateRaytracingPointLight(PointLight light,
	float3 worldPosition, ResolvedPBRMaterial material,
	float3 V, float3 F0, uint renderFlags) {

	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, renderFlags >> 8u)) return 0.0f.xxx;

	float3 toLight = light.pos - worldPosition;
	float distanceToLight = length(toLight);
	if (distanceToLight <= 1e-5f ||
		dot(material.N, toLight) <= 0.0f) {

		return 0.0f.xxx;
	}
	if (light.radius <= 0.0001f || distanceToLight >= light.radius || !any(light.color.rgb * light.intensity != 0.0f)) return 0;
	float3 L = toLight / distanceToLight;
	float shadow = ResolveRaytracingShadow(
		worldPosition, material.N, L,
		distanceToLight, light.shadowStrength, renderFlags, light.shadowFilterIndex);
	if (shadow == 0.0f) return 0;
	float attenuation = ComputeDistanceAttenuation(distanceToLight, light.radius, light.decay);
	return shadow * EvaluatePBRRadiance(material.N, V, L, light.color.rgb * (light.intensity * attenuation),
		material.baseColor.rgb, material.metallic, material.roughness, F0);
}

float3 EvaluateRaytracingSpotLight(SpotLight light,
	float3 worldPosition, ResolvedPBRMaterial material,
	float3 V, float3 F0, uint renderFlags) {

	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, renderFlags >> 8u)) return 0.0f.xxx;

	float3 toLight = light.pos - worldPosition;
	float distanceToLight = length(toLight);
	if (distanceToLight <= 1e-5f ||
		dot(material.N, toLight) <= 0.0f) {

		return 0.0f.xxx;
	}
	if (light.distance <= 0.0001f || distanceToLight >= light.distance || !any(light.color.rgb * light.intensity != 0.0f)) return 0;
	float3 L = toLight / distanceToLight;
	float cosTheta = dot(normalize(light.direction), -L);
	if (cosTheta <= light.cosAngle) return 0;
	float shadow = ResolveRaytracingShadow(
		worldPosition, material.N, L,
		distanceToLight, light.shadowStrength, renderFlags, light.shadowFilterIndex);
	if (shadow == 0.0f) return 0;
	float cone = saturate((cosTheta - light.cosAngle) / max(light.cosFalloffStart - light.cosAngle, 1e-4f));
	float attenuation = ComputeDistanceAttenuation(distanceToLight, light.distance, light.decay) * (cone * cone);
	return shadow * EvaluatePBRRadiance(material.N, V, L, light.color.rgb * (light.intensity * attenuation),
		material.baseColor.rgb, material.metallic, material.roughness, F0);
}

float3 EvaluateRaytracingRectLight(RectLight light,
	float3 worldPosition, ResolvedPBRMaterial material,
	float3 V, float3 F0, uint renderFlags) {

	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, renderFlags >> 8u)) return 0.0f.xxx;

	if (rectLTCMatrixIndex == kNoTexture || rectLTCAmplitudeIndex == kNoTexture || !any(GetRectLightRadiance(light))) return 0;
	float3 toCenter = light.pos - worldPosition;
	float centerDistance = length(toCenter);
	float halfHeight = 0.5f * (abs(dot(light.right, material.N)) * light.sourceWidth + abs(dot(light.up, material.N)) * light.sourceHeight);
	// 四隅すべてが受光面の裏ならLTCのクリップ結果も空になる
	float tolerance = 1e-6f * (length(light.pos) + length(worldPosition) + light.sourceWidth + light.sourceHeight);
	if (dot(toCenter, material.N) + halfHeight < -tolerance) return 0;
	if (ComputeRectRangeAttenuation(light, worldPosition) * ComputeRectBarnAttenuation(light, worldPosition) <= 0.0f ||
		light.sourceWidth <= 0.0f || light.sourceHeight <= 0.0f) return 0.0f;
	float shadow = ResolveRaytracingShadow(
		worldPosition, material.N,
		toCenter / max(centerDistance, 1e-5f), centerDistance,
		light.shadowStrength, renderFlags, light.shadowFilterIndex);

	if (shadow == 0.0f) return 0;
	return shadow * EvaluatePBRRectLight(light, worldPosition, material.N, V, material.baseColor.rgb,
		material.metallic, material.roughness, F0);
}

bool IsOnReflectionTriangle(float3 rasterPosition) {
	RaytracingInstanceShaderData instance = gRaytracingSceneInstances[InstanceID()];
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + GeometryIndex()];
	SubMeshShaderData subMesh = gRaytracingSubMeshes[geometry.subMeshDataIndex];
	StructuredBuffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.indexDescriptorIndex)];
	StructuredBuffer<MeshVertex> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexDescriptorIndex)];
	uint first = geometry.indexOffset + PrimitiveIndex() * 3u;
	float3 corners[3];
	[unroll]
	for (uint i = 0; i < 3; ++i) {
		MeshVertex vertex = vertices[instance.vertexOffset + indices[first + i]];
		corners[i] = mul(ObjectToWorld3x4(), mul(vertex.position, subMesh.localMatrix));
	}
	float3 e0 = corners[1] - corners[0], e1 = corners[2] - corners[0];
	float3 N = cross(e0, e1);
	float area = dot(N, N);
	if (area <= 1e-20f) return false;
	float3 delta = rasterPosition - corners[0];
	float2 uv = float2(dot(cross(delta, e1), N), dot(cross(e0, delta), N)) / area;
	if (any(uv < 0.0f) || uv.x + uv.y > 1.0f) return false;
	float3 magnitude = max(abs(corners[0]), abs(rasterPosition)) + abs(e0) + abs(e1);
	float tolerance = max(0.0001f, max(magnitude.x,max(magnitude.y,magnitude.z)) * 8.0f * 1.192093e-7f);
	return abs(dot(delta,N)) <= tolerance * sqrt(area);
}

bool MatchRasterSurface(float3 worldPosition,float3 normal,int2 pixel,float viewDepth,out float3 rasterNormal) {
	rasterNormal=0;

	if(pixel.x<0) return false;
	if((gSourceFlags.Load(int3(pixel,0))&kMaterialFlagSurface)==0u) return false;
	rasterNormal=DecodeWorldNormal(gSourceNormal.Load(int3(pixel,0)).xyz);
	if(dot(rasterNormal,normal)<0.95f) return false;
	float3 rasterPosition=gSourcePosition.Load(int3(pixel,0)).xyz;
	float3 delta=rasterPosition-worldPosition;
	float clipW=abs(viewDepth*gProjection[2][3]+gProjection[3][3]);
	float2 footprint=2.0f*clipW*gInvRenderSize/max(abs(float2(gProjection[0][0],gProjection[1][1])),1e-6f);
	float tolerance=max(max(footprint.x,footprint.y)*1.5f,0.0002f);
	if (dot(delta,delta)>tolerance*tolerance) return false;
	float nearLimit=min(tolerance,0.05f);
	if (dot(delta,delta)<=nearLimit*nearLimit && abs(dot(delta,rasterNormal))<=max(0.0002f,nearLimit*0.1f)) return true;
	return IsOnReflectionTriangle(rasterPosition);
}

float3 ReflectionWorldIrradiance(float3 worldPosition, float3 normal, bool needed) {

	Texture2D<float4> cache=ResourceDescriptorHeap[gGIWorldCacheIndex];
	Texture2D<float2> moments=ResourceDescriptorHeap[gGIWorldMomentsIndex];
	float4 cached = 0;
	if (needed) cached = SampleCachedIrradiance(cache, moments, worldPosition + 0.002f * normal, normal, false, true);
	float3 distant = GatherProbeIrradiance(cache, moments, worldPosition, normal, needed && cached.a < 1.0f);
	if (!needed) return 0;
	float3 irradiance=cached.rgb+(1.0f-cached.a)*distant;
	return min((irradiance+EmissiveIrradiance(worldPosition,normal,false))*indirectIntensity,60000.0f);
}

float3 ReflectionHitIrradiance(float3 worldPosition, float3 normal, int2 pixel, float viewDepth,
	bool matched, bool deferWorldGI, out bool deferred) {
	deferred = false;

	if(gGIResolvedIndex!=kNoTexture) {
		float3 rasterNormal;
		if(!matched) matched=MatchRasterSurface(worldPosition,normal,pixel,viewDepth,rasterNormal);
		if(matched) {
			Texture2D<float4> resolved=ResourceDescriptorHeap[gGIResolvedIndex];
			float4 gi=resolved.Load(int3(pixel,0));
			if(gi.a>=0.0f) return gi.rgb;
		}
	}
	deferred = deferWorldGI;
	return deferWorldGI ? 0 : ReflectionWorldIrradiance(worldPosition, normal, true);
}

float3 EvaluateRaytracingSurfaceLighting(
	float3 worldPosition,
	ResolvedPBRMaterial material,
	uint renderFlags, float transmission, int2 rasterPixel, bool deferWorldGI, out float3 deferredDiffuse) {

	deferredDiffuse = 0;

	if ((renderFlags & MESH_INSTANCE_FLAG_LIGHTING) == 0u) {
		return material.baseColor.rgb + material.emissive;
	}

	// 反射ヒットのBRDFは主カメラではなく、入射レイ方向を視線として評価する
	float3 V = SafeNormalize(-WorldRayDirection(), material.N);
	float3 F0 = lerp(
		gFresnelMin.xxx, material.baseColor.rgb, material.metallic);
	material.baseColor.rgb *= 1.0f - saturate(transmission);
	// 二次ヒット地点から影を判定し、主カメラの画面範囲に依存しない照明にする
	float3 lighting = 0.0f.xxx;
	[loop]
	for (uint index = 0u; index < directionalCount; ++index) {

		lighting += EvaluateRaytracingDirectionalLight(
			gDirectionalLights[index], worldPosition,
			material, V, F0, renderFlags);
	}

	// 画面内の疎なクラスタは連続リストを読み、画面外・密集部はワールド索引を使う
	LightClusterHeader cluster = (LightClusterHeader)0;
	float viewDepth = 0.0f;
	int2 pixel = rasterPixel;
	if (rasterPixel.x >= 0) {
		viewDepth = mul(float4(worldPosition, 1.0f), gView).z;
	} else if (localCount >= 24u || gGIResolvedIndex != kNoTexture) {
		if (!ProjectWorldToPixel(worldPosition, pixel, viewDepth)) pixel = int2(-1, -1);
	}
	bool useCluster = localCount >= 24u && ResolveRaytracingLightCluster(pixel, viewDepth, cluster);
	uint cursor = 0u, localIndex;
	WorldLightCursor worldCursor = (WorldLightCursor)0;
	if (!useCluster) worldCursor = BeginWorldLights(worldPosition);
	[loop]
	while (true) {
		if (useCluster) {
			if (cursor >= cluster.count) break;
			localIndex = gLightClusterIndices[cluster.offset + cursor++];
		} else if (!NextWorldLight(worldPosition, worldCursor, localIndex)) break;
		if (localIndex < pointCount) {
			lighting += EvaluateRaytracingPointLight(gPointLights[localIndex], worldPosition, material, V, F0, renderFlags);
		} else if (localIndex < pointCount + spotCount) {
			lighting += EvaluateRaytracingSpotLight(gSpotLights[localIndex - pointCount], worldPosition, material, V, F0, renderFlags);
		} else {
			lighting += EvaluateRaytracingRectLight(gRectLights[localIndex - pointCount - spotCount], worldPosition, material, V, F0, renderFlags);
		}
	}

	float3 ambient = 0.0f.xxx;
	if ((renderFlags & MESH_INSTANCE_FLAG_RECEIVE_IBL) != 0u) {

		if (gGIWorldCacheIndex != kNoTexture) {
			float3 diffuse = DiffuseGIReflectance(material.baseColor.rgb, material.metallic) * material.ao;
			if (any(diffuse) && indirectIntensity > 0.0f) {
				bool deferred;
				ambient = ReflectionHitIrradiance(worldPosition, material.N, pixel, viewDepth,
					rasterPixel.x >= 0, deferWorldGI, deferred) * diffuse;
				if (deferred) deferredDiffuse = diffuse;
			}
		} else if (gHasSkybox != 0u && gSkyboxCubemapIndex != kNoTexture) {

			TextureCube<float4> skybox = ResourceDescriptorHeap[
				NonUniformResourceIndex(gSkyboxCubemapIndex)];
			float3 irradiance;
			if (gPhysicalEnvironmentEnabled != 0u && gEnvironmentIrradianceIndex != kNoTexture) {
				TextureCube<float4> diffuseEnvironment = ResourceDescriptorHeap[NonUniformResourceIndex(gEnvironmentIrradianceIndex)];
				irradiance = diffuseEnvironment.SampleLevel(gEnvironmentSampler, material.N, 0.0f).rgb;
			} else {
				irradiance = skybox.SampleLevel(gEnvironmentSampler, material.N, 5.0f).rgb;
			}
			ambient = irradiance * gSkyboxColor.rgb * gIBLIntensity *
				material.baseColor.rgb * material.ao;
			if (gPhysicalEnvironmentEnabled != 0u) ambient *= (1.0f - material.metallic) * (1.0f - F0);
		} else {

			ambient = 0.03f * material.baseColor.rgb * material.ao;
		}
	}
	return lighting + ambient + material.emissive;
}

float3 EvaluateRaytracingSurfaceLighting(float3 worldPosition, ResolvedPBRMaterial material, uint renderFlags,
	float transmission = 0.0f, int2 rasterPixel = int2(-1, -1)) {

	float3 unused;
	return EvaluateRaytracingSurfaceLighting(worldPosition, material, renderFlags, transmission, rasterPixel, false, unused);
}

//============================================================================
//	miss
//============================================================================
[shader("miss")]
void ReflectionMiss(inout ReflectionPayload payload) {

	payload.hit = 0u;
	payload.color = 0.0f.xxx;
	payload.worldPosition = 0.0f.xxx;
	payload.hitDistance = 0.0f;
	payload.worldNormal = 0.0f.xxx;
}

//============================================================================
//	closesthit
//============================================================================
float3 ResolveReflectionTransparency(float3 color, float opacity, float transmission, float3 tint,
	bool refraction, float3 normal, float3 worldPosition, uint blendMode, uint depth) {

	opacity = saturate(opacity);
	if (blendMode == 0u && opacity == 1.0f) return color;
	float3 direction = WorldRayDirection();
	if (refraction) {
		direction = refract(direction, faceforward(normal, direction, normal), 1.0f / 1.5f);
	}
	float3 background;
	if (depth == 0u) {
		RayDesc ray;
		ray.Origin = worldPosition + direction * 0.001f;
		ray.Direction = direction;
		ray.TMin = 0.0001f;
		ray.TMax = gMaxReflectionDistance;
		ReflectionPayload behind = (ReflectionPayload)0;
		behind.transmissionDepth = 1u;
		TraceRay(gSceneTLAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
			kRaytracingMaskReflectionCaster, 0, 1, 0, ray, behind);
		background = behind.hit != 0u ? behind.color : EvaluateReflectionEnvironment(direction, 0.0f);
	} else {
		background = EvaluateReflectionEnvironment(direction, 0.0f);
	}
	if (refraction) {
		float transmitted = (1.0f - opacity) * saturate(transmission);
		color = (color * opacity + background * tint * transmitted) / max(opacity + transmitted, 0.0001f);
		opacity += transmitted;
	}
	switch (blendMode) {
	case 1u: return background + color * opacity;
	case 2u: return background - color * opacity;
	case 3u: return background * color;
	case 4u: return background + color * (1.0f - background);
	case 5u: return color + background * (1.0f - opacity);
	default: return lerp(background, color, opacity);
	}
}

#ifndef NEM_REFLECTION_CUSTOM_HIT
Texture2D<float4> gSourceEmissive : register(t0, space6);

// 同じ面が画面内にあるときはラスタライズ済みの材質を使う
bool LoadRasterHitMaterial(float3 worldPosition, inout ResolvedPBRMaterial material, out int2 rasterPixel) {

	rasterPixel = int2(-1, -1);

	int2 pixel;
	float viewDepth;
	float3 rasterNormal;
	if (!ProjectWorldToPixel(worldPosition,pixel,viewDepth) || !MatchRasterSurface(worldPosition,material.N,pixel,viewDepth,rasterNormal)) return false;
	rasterPixel = pixel;

	float4 packed = gSourceMaterial.Load(int3(pixel, 0));
	material.baseColor = gSourceAlbedo.Load(int3(pixel, 0));
	material.N = rasterNormal;
	material.metallic = saturate(packed.r);
	material.roughness = clamp(packed.g, 0.04f, 1.0f);
	material.ao = saturate(packed.b);
	material.emissive = gSourceEmissive.Load(int3(pixel, 0)).rgb;
	return true;
}

[shader("anyhit")]
void ReflectionAnyHit(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {

	RaytracingInstanceShaderData instance = gRaytracingSceneInstances[InstanceID()];
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + GeometryIndex()];
	SubMeshShaderData subMesh = gRaytracingSubMeshes[geometry.subMeshDataIndex];
	if ((subMesh._materialPad >> 16u) == 2u && !PassesHitAlpha(instance,geometry,subMesh,PrimitiveIndex(),attr.barycentrics)) IgnoreHit();
}

[shader("closesthit")]
void ReflectionClosestHit(
	inout ReflectionPayload payload,
	in BuiltInTriangleIntersectionAttributes attr) {

	RaytracingInstanceShaderData instanceData;
	float3 worldPosition;
	SubMeshShaderData subMesh;
	int2 rasterPixel;
	ResolvedPBRMaterial material = ResolveRaytracingHitMaterial(
		attr, instanceData, worldPosition, subMesh, rasterPixel);
	// 画面外の投影も命中点の照明前に合成する
	const float footprint = 2.0f / max(1e-5f, abs(gProjection[1][1]) * gRenderSize.y) *
		(gProjection[3][3] > 0.5f ? 1.0f : length(worldPosition - gCameraPosition));
	// GBufferに合成済みのDecalを重ねない
	if (rasterPixel.x < 0) {
		if (gDecalCount != 0u || gGPUImpactDecalSummaryCount != 0u) {
			ApplyRaytracingDecals(instanceData.geometryDataOffset + GeometryIndex(), worldPosition, footprint,
				instanceData.renderFlags >> 8u, gSampler, material.baseColor.rgb, material.N,
				material.metallic, material.roughness, material.emissive);
		} else material.N = normalize(material.N);
	}

	payload.hit = 1u;
	bool deferWorldGI = payload.deferredDiffuse.x < 0.0f && (subMesh._materialPad >> 16u) != 3u && payload.transmissionDepth == 0u;
	payload.color = EvaluateRaytracingSurfaceLighting(worldPosition, material, instanceData.renderFlags,
		0.0f, rasterPixel, deferWorldGI, payload.deferredDiffuse);
	if ((subMesh._materialPad >> 16u) == 3u) {
		payload.color = ResolveReflectionTransparency(payload.color, material.baseColor.a, 0.0f, 1.0f.xxx,
			false, material.N, worldPosition, subMesh.renderGroupIndex, payload.transmissionDepth);
	}
	payload.worldPosition = worldPosition;
	payload.hitDistance = RayTCurrent();
	payload.worldNormal = material.N;
}
#endif

//============================================================================
//	raygeneration
//============================================================================
#ifndef NEM_REFLECTION_CUSTOM_RAY_GENERATION
void StoreReflectionTrace(uint2 pixel, float4 color, float hitDistance, float4 hitGeometry) {

	gDestColor[pixel] = color;
	gReflectionHitDistance[pixel] = hitDistance;
	gReflectionHitGeometry[pixel] = hitGeometry;
}

[shader("raygeneration")]
void ReflectionRayGen() {

	uint2 pixel = DispatchRaysIndex().xy;
	uint2 dim = DispatchRaysDimensions().xy;
	uint2 sourceDim;
	gSourcePosition.GetDimensions(sourceDim.x, sourceDim.y);
	float2 sourceUV = (float2(pixel) + 0.5f) / float2(dim);
	uint2 sourcePixel = min(
		uint2(sourceUV * float2(sourceDim)), sourceDim - 1u);

	uint surfaceFlags = gSourceFlags.Load(int3(sourcePixel, 0));

	// 背景画素はLightingPassがskyboxを書いているのでそのまま残す
	if ((surfaceFlags & kMaterialFlagSurface) == 0u) {
		StoreReflectionTrace(pixel, 0.0f.xxxx, 0.0f, 0.0f.xxxx);
		return;
	}
	// 反射を受けないサーフェイスはそのまま残す
	if ((surfaceFlags & kMaterialFlagReceiveReflection) == 0u) {
		StoreReflectionTrace(pixel, 0.0f.xxxx, 0.0f, 0.0f.xxxx);
		return;
	}

	float3 albedo = gSourceAlbedo.Load(int3(sourcePixel, 0)).rgb;
	float4 material = gSourceMaterial.Load(int3(sourcePixel, 0));
	float metallic = saturate(material.r);
	float roughness = clamp(material.g, 0.04f, 1.0f);
	float roughnessFadeStart = max(
		gReflectionMaxRoughness - gReflectionRoughnessFade, 0.0f);
	float roughnessVisibility = 1.0f - smoothstep(
		roughnessFadeStart, gReflectionMaxRoughness, roughness);
	if (roughnessVisibility <= 0.0f) {
		StoreReflectionTrace(pixel, 0.0f.xxxx, 0.0f, 0.0f.xxxx);
		return;
	}

	float3 worldPos = LoadPrimaryWorldPosition(sourcePixel);
	float3 worldNormal = DecodeWorldNormal(
		gSourceNormal.Load(int3(sourcePixel, 0)).xyz);
	if (dot(worldNormal, worldNormal) <= 1e-6f) {
		worldNormal = EstimateWorldNormalFromPosition(
			sourcePixel, sourceDim);
	}

	float3 V = SafeNormalize(
		gCameraPosition - worldPos, float3(0.0f, 0.0f, 1.0f));

	float VdotH = saturate(dot(V, worldNormal));
	float3 direction = SafeNormalize(reflect(-V, worldNormal), worldNormal);
	if (VdotH <= 0.0f || dot(worldNormal, direction) <= 0.0f) {
		StoreReflectionTrace(pixel, 0.0f.xxxx, 0.0f, 0.0f.xxxx);
		return;
	}
	float worldFootprint = EstimatePrimaryWorldFootprint(sourcePixel, sourceDim, worldPos, worldNormal);
	float NdotV = max(VdotH, 1e-4f);
	float grazing = 1.0f - NdotV;

	float adaptiveNormalBias = max(
		gReflectionNormalBias, worldFootprint * 0.50f);
	float adaptiveViewBias = max(
		gReflectionViewBias,
		worldFootprint * lerp(0.25f, 1.50f, grazing));
	float adaptiveMinHitDistance = max(
		gReflectionMinHitDistance,
		worldFootprint * lerp(1.00f, 2.00f, grazing));

	float3 F0 = lerp(gFresnelMin.xxx, albedo, metallic);
	float3 sampleWeight = FresnelSchlick(VdotH, F0);
	if (gPhysicalEnvironmentEnabled != 0u && gEnvironmentBRDFIndex != kNoTexture) {
		Texture2D<float4> brdf = NEM_TEXTURE2D(gEnvironmentBRDFIndex);
		float2 integral = brdf.SampleLevel(gEnvironmentSampler, float2(NdotV, roughness), 0.0f).rg;
		sampleWeight = F0 * integral.x + integral.y;
	}

	RayDesc ray;
	ray.Origin = worldPos + worldNormal * adaptiveNormalBias + direction * adaptiveViewBias;
	ray.Direction = direction;
	ray.TMin = adaptiveMinHitDistance;
	ray.TMax = gMaxReflectionDistance;
	ReflectionPayload payload = (ReflectionPayload)0;
	// 標準の一次レイのみ、画面外の間接光をヒット後にまとめて評価する
	payload.deferredDiffuse = -1.0f.xxx;
	TraceRay(gSceneTLAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
		kRaytracingMaskReflectionCaster, 0, 1, 0, ray, payload);

	float3 incoming = payload.color;
	bool deferred = payload.hit != 0u && any(payload.deferredDiffuse > 0);
	if (WaveActiveAnyTrue(deferred)) {
		float3 gi = ReflectionWorldIrradiance(payload.worldPosition, payload.worldNormal, deferred);
		if (deferred) incoming += gi * payload.deferredDiffuse;
	}
	if (payload.hit == 0u) incoming = EvaluateReflectionEnvironment(direction, roughness);
	float3 radiance = incoming * sampleWeight * (gReflectionIntensity * roughnessVisibility);
	StoreReflectionTrace(pixel, float4(StoreReflectionRadiance(radiance), roughnessVisibility),
		payload.hit != 0u ? payload.hitDistance : gMaxReflectionDistance,
		payload.hit != 0u ? float4(payload.worldPosition, 1.0f) : 0.0f.xxxx);
}
#endif
