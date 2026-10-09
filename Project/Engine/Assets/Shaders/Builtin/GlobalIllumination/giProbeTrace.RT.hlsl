#define NEM_REFLECTION_HELPERS_ONLY
#define NEM_REFLECTION_CUSTOM_HIT
#include "../Raytracing/reflection.RT.hlsl"
#include "giProbeSampling.hlsli"
#include "giMaterialData.hlsli"
#include "../Primitive/primitiveInstanceColor.hlsli"

RWTexture2D<float4> gGIRayResults : register(u0, space6);

bool ResolveGIMaterial(in BuiltInTriangleIntersectionAttributes attr, out GIMaterialData data) {

	data = (GIMaterialData)0;
	RaytracingInstanceShaderData instance = gRaytracingSceneInstances[InstanceID()];
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[instance.geometryDataOffset + GeometryIndex()];
	if (geometry.giCallableIndex == 0xFFFFFFFFu) return false;
	SubMeshShaderData subMesh;
	ResolveRaytracingHitGeometry(attr, instance, subMesh, data.uv, data.worldPosition, data.tangentToWorld);
	data.worldNormal = data.tangentToWorld[2];
	StructuredBuffer<uint> indices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.indexDescriptorIndex)];
	StructuredBuffer<MeshVertex> vertices = ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexDescriptorIndex)];
	uint baseIndex = geometry.indexOffset + PrimitiveIndex() * 3u;
	MeshVertex v0 = vertices[instance.vertexOffset + indices[baseIndex]];
	MeshVertex v1 = vertices[instance.vertexOffset + indices[baseIndex + 1u]];
	MeshVertex v2 = vertices[instance.vertexOffset + indices[baseIndex + 2u]];
	float3 bary = ComputeBarycentrics(attr.barycentrics);
	data.vertexColor = 1.0f.xxxx;
	if (geometry.giPrimitiveColorDescriptor != 0xFFFFFFFFu) {

		ConstantBuffer<PrimitiveInstance> shape = ResourceDescriptorHeap[NonUniformResourceIndex(geometry.giPrimitiveColorDescriptor)];
		StructuredBuffer<MeshVertex> original = ResourceDescriptorHeap[NonUniformResourceIndex(geometry.giOriginalVertexDescriptor)];
		PrimitiveInstance primitive = shape;
		data.vertexColor = ResolvePrimitiveVertexColor(original[indices[baseIndex]].position.xyz, primitive) * bary.x +
			ResolvePrimitiveVertexColor(original[indices[baseIndex + 1u]].position.xyz, primitive) * bary.y +
			ResolvePrimitiveVertexColor(original[indices[baseIndex + 2u]].position.xyz, primitive) * bary.z;
	}
	data.objectPosition = v0.position.xyz * bary.x + v1.position.xyz * bary.y + v2.position.xyz * bary.z;
	data.objectNormal = SafeNormalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z, data.worldNormal);
	data.objectTangent = SafeNormalize(v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z, data.tangentToWorld[0]);
	data.objectPosition = mul(float4(data.objectPosition, 1.0f), subMesh.localMatrix).xyz;
	data.objectNormal = SafeNormalize(mul(float4(data.objectNormal, 0.0f), subMesh.localNormalMatrix).xyz, data.worldNormal);
	data.objectTangent = SafeNormalize(mul(float4(data.objectTangent, 0.0f), subMesh.localMatrix).xyz, data.tangentToWorld[0]);
	data.viewDirection = SafeNormalize(gCameraPosition - data.worldPosition, -WorldRayDirection());
	float4 clip = mul(mul(float4(data.worldPosition, 1.0f), gView), gProjection);
	float2 screenUV = clip.xy / max(abs(clip.w), 1e-6f) * float2(0.5f, -0.5f) + 0.5f;
	data.screenPosition = float4(screenUV * gRenderSize, clip.z / max(abs(clip.w), 1e-6f), 1.0f);
	data.parametersDescriptor = geometry.giParametersDescriptor;
	CallShader(geometry.giCallableIndex, data);
	return true;
}

float GIShadow(float3 position, float3 normal, float3 direction, float maxDistance, float strength, uint flags) {

	if (strength <= 0.0f || (flags & MESH_INSTANCE_FLAG_RECEIVE_SHADOW) == 0u || maxDistance <= 0.002f) return 1.0f;
	RayDesc ray;
	ray.Origin = position + normal * max(gShadowNormalBias, 0.0001f);
	ray.Direction = SafeNormalize(direction, normal);
	ray.TMin = 0.001f;
	ray.TMax = max(maxDistance - 0.001f, ray.TMin);
	while (ray.TMin < ray.TMax) {

		ReflectionPayload payload = (ReflectionPayload)0;
		payload.hit = 3u;
		TraceRay(gSceneTLAS, RAY_FLAG_FORCE_NON_OPAQUE, 1u, 0u, 0u, 0u, ray, payload);
		if (payload.hit != 2u) return payload.hit == 1u ? 1.0f - saturate(strength) : 1.0f;
		ray.TMin = max(ray.TMin + 0.0001f, payload.hitDistance + 0.0001f);
	}
	return 1.0f;
}

float3 GIDiffuseLight(float3 worldPosition, ResolvedPBRMaterial material, uint flags) {

	// Unlitは通常描画と同じ色を間接光へ渡す
	if ((flags & MESH_INSTANCE_FLAG_LIGHTING) == 0u) return max(material.baseColor.rgb + material.emissive, 0.0f.xxx);

	// 命中面の拡散光だけを次の反射へ渡す
	float3 irradiance = 0.0f.xxx;
	[loop]
	for (uint i = 0u; i < directionalCount; ++i) {

		DirectionalLight light = gDirectionalLights[i];
		float3 L = SafeNormalize(-light.direction, material.N);
		float cosine = saturate(dot(material.N, L));
		if (cosine <= 0.0f) continue;
		float shadow = GIShadow(worldPosition, material.N, L, giMaxRayDistance, light.shadowStrength, flags);
		irradiance += light.color.rgb * light.intensity * cosine * shadow;
	}
	[loop]
	for (uint i = 0u; i < pointCount; ++i) {

		PointLight light = gPointLights[i];
		float3 delta = light.pos - worldPosition;
		float distanceValue = length(delta);
		if (distanceValue <= 1e-5f) continue;
		float3 L = delta / distanceValue;
		float attenuation = ComputeDistanceAttenuation(distanceValue, light.radius, light.decay);
		float cosine = saturate(dot(material.N, L));
		if (attenuation * cosine <= 0.0f) continue;
		float shadow = GIShadow(worldPosition, material.N, L, distanceValue, light.shadowStrength, flags);
		irradiance += light.color.rgb * light.intensity * attenuation * cosine * shadow;
	}
	[loop]
	for (uint i = 0u; i < spotCount; ++i) {

		SpotLight light = gSpotLights[i];
		float3 delta = light.pos - worldPosition;
		float distanceValue = length(delta);
		if (distanceValue <= 1e-5f) continue;
		float3 L = delta / distanceValue;
		float attenuation = ComputeDistanceAttenuation(distanceValue, light.distance, light.decay);
		float cone = saturate((dot(-L, light.direction) - light.cosAngle) / max(light.cosFalloffStart - light.cosAngle, 1e-5f));
		float cosine = saturate(dot(material.N, L));
		if (attenuation * cone * cosine <= 0.0f) continue;
		float shadow = GIShadow(worldPosition, material.N, L, distanceValue, light.shadowStrength, flags);
		irradiance += light.color.rgb * light.intensity * attenuation * cone * cone * cosine * shadow;
	}
	[loop]
	for (uint i = 0u; i < rectCount; ++i) {

		RectLight light = gRectLights[i];
		float attenuation = ComputeDistanceAttenuation(length(light.pos - worldPosition), light.attenuationRadius, light.decay);
		attenuation *= ComputeRectBarnAttenuation(light, worldPosition);
		if (attenuation <= 0.0f) continue;
		[unroll]
		for (uint sampleIndex = 0u; sampleIndex < kRectLightSampleCount; ++sampleIndex) {

			float3 delta = GetRectLightSamplePosition(light, sampleIndex) - worldPosition;
			float distanceValue = length(delta);
			float3 L = delta / max(distanceValue, 1e-5f);
			float shadow = GIShadow(worldPosition, material.N, L, distanceValue, light.shadowStrength, flags);
			irradiance += light.color.rgb * light.intensity * attenuation * saturate(dot(material.N, L)) *
				saturate(dot(-L, light.direction)) * shadow / float(kRectLightSampleCount);
		}
	}
	float4 previous = SampleGlobalIllumination(worldPosition, material.N, -WorldRayDirection());
	float3 diffuse = saturate(material.baseColor.rgb) * (1.0f - saturate(material.metallic)) * 0.96f / PI;
	return max(material.emissive, 0.0f.xxx) + diffuse * (irradiance + previous.rgb * previous.a);
}

[shader("closesthit")]
void GIClosestHit(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {

	const bool shadowRay = payload.hit == 3u;
	RaytracingInstanceShaderData instanceData;
	float3 position;
	ResolvedPBRMaterial material = ResolveRaytracingHitMaterial(attr, instanceData, position);
	GIMaterialData graph;
	if (ResolveGIMaterial(attr, graph)) {

		if (graph.baseColor.a * graph.opacity < graph.alphaClip) {

			payload.hit = 2u;
			payload.hitDistance = RayTCurrent();
			return;
		}
		material.baseColor = graph.baseColor;
		material.N = graph.normal;
		material.metallic = graph.metallic;
		material.roughness = graph.roughness;
		material.ao = graph.ambientOcclusion;
		material.emissive = graph.emissive;
	}
	if (shadowRay) payload.color = 0.0f.xxx;
	else payload.color = GIDiffuseLight(position, material, instanceData.renderFlags);
	payload.hit = 1u;
	payload.hitDistance = RayTCurrent();
	payload._pad0 = HitKind() == HIT_KIND_TRIANGLE_BACK_FACE ? 1.0f : 0.0f;
}

[shader("anyhit")]
void GIAnyHit(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {

	RaytracingInstanceShaderData source = gRaytracingSceneInstances[InstanceID()];
	RaytracingGeometryShaderData geometry = gRaytracingGeometries[source.geometryDataOffset + GeometryIndex()];
	if (geometry.giCallableIndex != 0xFFFFFFFFu) return;
	// 不透明面はTextureによる切抜き判定を省く
	if (asfloat(gRaytracingSubMeshes[geometry.subMeshDataIndex]._materialPad) <= 0.0f) return;
	RaytracingInstanceShaderData instanceData;
	SubMeshShaderData subMesh;
	float2 uv;
	float3 position;
	float3x3 tangentToWorld;
	ResolveRaytracingHitGeometry(attr, instanceData, subMesh, uv, position, tangentToWorld);
	float alpha = subMesh.color.a * subMesh.importedBaseColor.a *
		SampleHitTexture(subMesh.baseColorTextureIndex, uv, 0.0f, 1.0f.xxxx).a *
		SampleHitTexture(subMesh.opacityTextureIndex, uv, 0.0f, 1.0f.xxxx).r;
	if (alpha < asfloat(subMesh._materialPad)) IgnoreHit();
}

[shader("miss")]
void GIMiss(inout ReflectionPayload payload) {

	payload.color = gHasSkybox != 0u ? EvaluateReflectionEnvironment(WorldRayDirection()) * gIBLIntensity : 0.0f.xxx;
	payload.hit = 0u;
	payload.hitDistance = giMaxRayDistance;
	payload._pad0 = 0.0f;
}

[shader("raygeneration")]
void GIProbeRayGen() {

	uint2 pixel = DispatchRaysIndex().xy;
	uint probeCount = giGridCount * giGridCount * giGridCount * giLevelCount;
	uint probeIndex = (giStartProbe + pixel.y) % probeCount;
	float3 position = GINominalProbePosition(probeIndex);
	float4 oldPosition = gGIPositions.Load(int3(probeIndex, 0, 0));
	float spacing = giGridOrigins[probeIndex / (giGridCount * giGridCount * giGridCount)].w;
	if (giResetHistory == 0u && oldPosition.w > 0.0f && all(abs(oldPosition.xyz - position) < spacing * 0.01f)) {
		position += gGIOffsets.Load(int3(probeIndex, 0, 0)).xyz;
	}
	RayDesc ray;
	ray.Origin = position;
	ray.Direction = GIRayDirection(pixel.x);
	ray.TMin = max(spacing * 0.001f, 0.0001f);
	ray.TMax = giMaxRayDistance;
	ReflectionPayload payload = (ReflectionPayload)0;
	// Graphで切り抜かれた面の先を追跡
	while (ray.TMin < ray.TMax) {

		payload = (ReflectionPayload)0;
		TraceRay(gSceneTLAS, RAY_FLAG_FORCE_NON_OPAQUE, 4u, 0u, 0u, 0u, ray, payload);
		if (payload.hit != 2u) break;
		ray.TMin = max(ray.TMin + 0.0001f, payload.hitDistance + 0.0001f);
	}
	// 最大距離まで切り抜かれたRayはSkyboxへ接続
	if (payload.hit == 2u) {

		payload.color = gHasSkybox != 0u ? EvaluateReflectionEnvironment(ray.Direction) * gIBLIntensity : 0.0f.xxx;
		payload.hitDistance = giMaxRayDistance;
		payload._pad0 = 0.0f;
	}
	float3 radiance = max(payload.color, 0.0f.xxx);
	if (!all(isfinite(radiance))) radiance = 0.0f.xxx;
	if (payload._pad0 != 0.0f) radiance = 0.0f.xxx;
	gGIRayResults[pixel] = float4(min(radiance, 60000.0f.xxx), payload._pad0 != 0.0f ? -payload.hitDistance : payload.hitDistance);
}
