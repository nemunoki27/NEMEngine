#ifndef NEM_LOCAL_DIRECT_LIGHTING_HLSLI
#define NEM_LOCAL_DIRECT_LIGHTING_HLSLI

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
StructuredBuffer<LightClusterHeader> gLightClusterHeaders : register(t9);
StructuredBuffer<uint> gLightClusterIndices : register(t11);

float3 EvaluatePBRLight(float3 N, float3 V, float3 L, float3 radiance,
	float3 albedo, float metallic, float roughness, float3 F0) {

	float NdotL = saturate(dot(N, L));
	if (NdotL <= 0.0f) {
		return 0.0f.xxx;
	}

	float NdotV = saturate(dot(N, V));
	float3 H = normalize(V + L);
	float NdotH = saturate(dot(N, H));
	float HdotV = saturate(dot(H, V));
	float LdotH = saturate(dot(L, H));

	float3 F = FresnelSchlick(HdotV, F0);
	float D = EvalD(NdotH, roughness);
	float G = EvalG(NdotV, NdotL, roughness);

	float3 specular = D * G * F / max(4.0f * NdotV * NdotL, 1e-4f);
	float3 kD = (1.0f - F) * (1.0f - metallic);
	float3 diffuse = kD * DisneyDiffuse(NdotL, NdotV, LdotH, roughness, albedo);

	return (diffuse + specular) * NdotL * radiance;
}

float3 EvaluatePointLightIndex(uint lightIndex,
	float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0,
	uint flags, bool useShadow, uint2 pixel) {

	PointLight light = gPointLights[lightIndex];
	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, flags >> 8u)) return 0.0f.xxx;
	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f || dot(N, toLight) <= 0.0f) {
		return 0.0f.xxx;
	}
	float attenuation =
		ComputeDistanceAttenuation(dist, light.radius, light.decay);
	if (attenuation <= 0.0f) {
		return 0.0f.xxx;
	}
	float3 L = toLight / dist;
	float shadow = 1.0f;
	if (useShadow && light.shadowStrength > 0.0f &&
		(flags & kMaterialFlagReceiveShadow) != 0u) {
		float occlusion = -1.0f;
#ifndef NEM_RESTIR_SHADE
		if (filteredShadowAvailable != 0u && restirDIAvailable == 0u) occlusion = LoadFilteredShadow(pixel, directionalCount + lightIndex);
#endif
		if (occlusion < 0.0f) occlusion = TraceLocalSoftShadow(worldPos, N, toLight, dist, light.shadowRadius, pixel, lightIndex, light.shadowFilterIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}
	if (shadow <= 0.0f) return 0.0f.xxx;
	float3 radiance =
		light.color.rgb * light.intensity * attenuation * shadow;
	return EvaluatePBRLight(
		N, V, L, radiance, albedo, metallic, roughness, F0);
}

float3 EvaluateSpotLightIndex(uint lightIndex,
	float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0,
	uint flags, bool useShadow, uint2 pixel) {

	SpotLight light = gSpotLights[lightIndex];
	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, flags >> 8u)) return 0.0f.xxx;
	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f || dot(N, toLight) <= 0.0f) {
		return 0.0f.xxx;
	}
	if (light.distance <= 0.0001f || dist >= light.distance) {
		return 0.0f.xxx;
	}
	float3 L = toLight / dist;
	float3 lightDir = normalize(light.direction);
	float cosTheta = dot(-L, lightDir);
	float coneRange =
		max(light.cosFalloffStart - light.cosAngle, 1e-4f);
	float coneAttenuation =
		saturate((cosTheta - light.cosAngle) / coneRange);
	coneAttenuation *= coneAttenuation;
	if (coneAttenuation <= 0.0f) {
		return 0.0f.xxx;
	}
	float distanceAttenuation = ComputeDistanceAttenuation(dist, light.distance, light.decay);
	if (distanceAttenuation <= 0.0f) return 0.0f.xxx;
	float shadow = 1.0f;
	if (useShadow && light.shadowStrength > 0.0f &&
		(flags & kMaterialFlagReceiveShadow) != 0u) {
		float occlusion = -1.0f;
#ifndef NEM_RESTIR_SHADE
		if (filteredShadowAvailable != 0u && restirDIAvailable == 0u) occlusion = LoadFilteredShadow(pixel, directionalCount + pointCount + lightIndex);
#endif
		if (occlusion < 0.0f) occlusion = TraceLocalSoftShadow(worldPos, N, toLight, dist, light.shadowRadius, pixel, pointCount + lightIndex, light.shadowFilterIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}
	if (shadow <= 0.0f) return 0.0f.xxx;
	float3 radiance = light.color.rgb * light.intensity *
		distanceAttenuation * coneAttenuation * shadow;
	return EvaluatePBRLight(
		N, V, L, radiance, albedo, metallic, roughness, F0);
}

float3 EvaluateRectLightIndex(uint lightIndex,
	float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0,
	uint flags, bool useShadow, uint2 pixel) {

	RectLight light = gRectLights[lightIndex];
	if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, flags >> 8u)) return 0.0f.xxx;
	if (ComputeRectRangeAttenuation(light, worldPos) * ComputeRectBarnAttenuation(light, worldPos) <= 0.0f ||
		light.sourceWidth <= 0.0f || light.sourceHeight <= 0.0f) return 0.0f;
	// 四隅がすべて受光面の裏なら、LTC積分とシャドウレイの寄与はゼロ
	float3 halfRight = light.right * (light.sourceWidth * 0.5f);
	float3 halfUp = light.up * (light.sourceHeight * 0.5f);
	float furthest = dot(light.pos - worldPos, N) + abs(dot(halfRight, N)) + abs(dot(halfUp, N));
	float roundoff = dot(abs(light.pos) + abs(worldPos) + abs(halfRight) + abs(halfUp), abs(N)) * 1e-6f;
	if (furthest < -roundoff) return 0.0f.xxx;
	float shadow = 1.0f;
	if (useShadow && light.shadowStrength > 0.0f &&
		(flags & kMaterialFlagReceiveShadow) != 0u) {

		float occlusion = -1.0f;
#ifndef NEM_RESTIR_SHADE
		if (filteredShadowAvailable != 0u && restirDIAvailable == 0u) occlusion = LoadFilteredShadow(pixel, directionalCount + pointCount + spotCount + lightIndex);
#endif
		if (occlusion < 0.0f) occlusion = TraceRectShadow(worldPos, N, light, pixel, pointCount + spotCount + lightIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}

	if (shadow <= 0.0f) return 0.0f.xxx;
	return shadow * EvaluateRectAreaPBR(light, worldPos, N, V, albedo, metallic, roughness, F0);
}

bool ResolveLightCluster(int2 pixel, float3 worldPos,
	out LightClusterHeader header) {

	header.offset = 0;
	header.count = 0;
	if (clusterCount == 0u || clusterTileCountX == 0u ||
		clusterTileCountY == 0u || clusterZSliceCount == 0u) {
		return false;
	}

	uint2 tile = min(
		uint2(max(pixel, int2(0, 0))) / max(clusterTileSize, 1u),
		uint2(clusterTileCountX - 1u, clusterTileCountY - 1u));
	float viewDepth = mul(float4(worldPos, 1.0f), viewMatrix).z;
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
	return true;
}

float3 EvaluateLocalLight(uint lightIndex, float3 pos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0, uint flags, bool useShadow, uint2 pixel) {

	if (lightIndex < pointCount) {
		return EvaluatePointLightIndex(lightIndex, pos, N, V, albedo, metallic, roughness, F0, flags, useShadow, pixel);
	}
	lightIndex -= pointCount;
	if (lightIndex < spotCount) {
		return EvaluateSpotLightIndex(lightIndex, pos, N, V, albedo, metallic, roughness, F0, flags, useShadow, pixel);
	}
	lightIndex -= spotCount;
	if (lightIndex < rectCount) {
		return EvaluateRectLightIndex(lightIndex, pos, N, V, albedo, metallic, roughness, F0, flags, useShadow, pixel);
	}
	return 0.0f.xxx;
}

#endif
