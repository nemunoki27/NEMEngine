//============================================================================
//	include
//============================================================================
#include "../FullscreenCopy/fullscreenCopy.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"
#include "../Common/pbrMath.hlsli"
#include "../Common/descriptorHeapCompatibility.hlsli"

//============================================================================
//	GBuffer入力
//============================================================================
Texture2D<float4> gAlbedo : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gWorldPos : register(t2);
Texture2D<float4> gMaterial : register(t3);
Texture2D<float4> gEmissive : register(t4);
Texture2D<uint> gFlags : register(t5);
Texture2D<float4> gAmbientOcclusion : register(t17);
StructuredBuffer<float2> gFilteredRaytracedShadows : register(t18);
StructuredBuffer<uint> gShadowLightIndices : register(t19);

SamplerState gSampler : register(s0);

//============================================================================
//	ライト
//============================================================================
#include "lightShaderData.hlsli"

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

//============================================================================
//	ライティングパス定数
//============================================================================

cbuffer DeferredLightingConstants : register(b1) {

	float3 cameraPos;
	float ambientIntensity;

	float4x4 inverseViewProjection;
	float4x4 viewMatrix;
	float4x4 viewProjectionMatrix;

	float4 skyboxColor;

	uint skyboxCubemapIndex;
	uint hasSkybox;
	uint2 viewportSize;

	float shadowNormalBias;
	float shadowMaxDistance;
	// Skyboxから畳み込んだ放射照度cubemap、無い場合はkNoCubemap
	uint irradianceCubemapIndex;
	// 拡散IBL環境光の強さ
	float iblIntensity;

	uint softShadowSampleCount;
	uint shadowMapAvailable;
	uint shadowMapLightIndex;
	uint reflectionFeatureActive;

	float ambientOcclusionIntensity;
	float reflectionIntensity;
	uint filteredShadowAvailable;
	float _surfacePad;

	float4x4 shadowViewProjections[4];
	float4 shadowCascadeSplits;
	float4 shadowDepthRanges;
};

Texture2D<float> gDirectionalShadowMap0 : register(t13);
Texture2D<float> gDirectionalShadowMap1 : register(t14);
Texture2D<float> gDirectionalShadowMap2 : register(t15);
Texture2D<float> gDirectionalShadowMap3 : register(t16);

// 無効キューブマップインデックス
static const uint kNoCubemap = 0xFFFFFFFF;

//============================================================================
//	平行光源影
//============================================================================

#include "inlineShadowCommon.hlsli"

//============================================================================
//	PBR lighting
//============================================================================
// ライトごとに平滑化した遮蔽率を参照する
float LoadFilteredShadow(uint2 pixel, uint lightIndex) {

	uint slot = gShadowLightIndices[lightIndex];
	if (slot == 0xFFFFFFFFu) return 0.0f;
	return saturate(gFilteredRaytracedShadows[(slot * viewportSize.y + pixel.y) * viewportSize.x + pixel.x].x);
}

float ComputeDistanceAttenuation(float dist, float range, float decay) {

	if (range <= 0.0001f || dist >= range) {
		return 0.0f;
	}

	float x = saturate(dist / range);
	float smooth = 1.0f - x * x;
	smooth *= smooth;

	float d = max(decay, 0.0f);
	float distanceFalloff = 1.0f / max(pow(max(dist, 1.0f), d), 1.0f);

	return smooth * distanceFalloff;
}

// バーンドアで矩形外側へ広がる光の範囲と境界の鋭さを制御する
float ComputeRectBarnAttenuation(RectLight light, float3 worldPos) {

	float3 fromLight = worldPos - light.pos;
	float forward = dot(fromLight, light.direction);
	if (forward <= 0.0f) {
		return 0.0f;
	}
	if (light.barnDoorLength <= 0.0001f) {
		return 1.0f;
	}

	float spread = tan(radians(clamp(
		light.barnDoorAngle, 0.0f, 89.0f))) * forward;
	float widthLimit = max(light.sourceWidth, 0.0f) * 0.5f + spread;
	float heightLimit = max(light.sourceHeight, 0.0f) * 0.5f + spread;
	float lateralX = abs(dot(fromLight, light.right));
	float lateralY = abs(dot(fromLight, light.up));
	float edgeSoftness = max(
		forward / (1.0f + light.barnDoorLength * 4.0f), 0.001f);

	float widthAttenuation =
		1.0f - smoothstep(widthLimit, widthLimit + edgeSoftness, lateralX);
	float heightAttenuation =
		1.0f - smoothstep(heightLimit, heightLimit + edgeSoftness, lateralY);
	return widthAttenuation * heightAttenuation;
}

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
float3 Square(float3 value) {
	return value * value;
}

float3 EvaluatePointLightIndex(uint lightIndex,
	float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0,
	uint flags, bool useShadow, uint2 pixel) {

	PointLight light = gPointLights[lightIndex];
	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f) {
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
		float occlusion = filteredShadowAvailable != 0u ? LoadFilteredShadow(pixel, directionalCount + lightIndex) :
			TraceLocalSoftShadow(worldPos, N, toLight, dist, light.shadowRadius, pixel, lightIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}
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
	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f) {
		return 0.0f.xxx;
	}
	float distanceAttenuation =
		ComputeDistanceAttenuation(dist, light.distance, light.decay);
	if (distanceAttenuation <= 0.0f) {
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
	float shadow = 1.0f;
	if (useShadow && light.shadowStrength > 0.0f &&
		(flags & kMaterialFlagReceiveShadow) != 0u) {
		float occlusion = filteredShadowAvailable != 0u ? LoadFilteredShadow(pixel, directionalCount + pointCount + lightIndex) :
			TraceLocalSoftShadow(worldPos, N, toLight, dist, light.shadowRadius, pixel, pointCount + lightIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}
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
	float centerDistance = length(light.pos - worldPos);
	float attenuation = ComputeDistanceAttenuation(
		centerDistance, light.attenuationRadius, light.decay);
	float barnAttenuation =
		ComputeRectBarnAttenuation(light, worldPos);
	if (attenuation <= 0.0f || barnAttenuation <= 0.0f) {
		return 0.0f.xxx;
	}

	float shadow = 1.0f;
	if (useShadow && light.shadowStrength > 0.0f &&
		(flags & kMaterialFlagReceiveShadow) != 0u) {

		float occlusion = filteredShadowAvailable != 0u ?
			LoadFilteredShadow(pixel, directionalCount + pointCount + spotCount + lightIndex) :
			TraceRectShadow(worldPos, N, light, pixel, pointCount + spotCount + lightIndex);
		shadow = 1.0f - occlusion * light.shadowStrength;
	}

	float3 result = 0.0f.xxx;
	[unroll]
	for (uint sampleIndex = 0u;
		sampleIndex < kMaximumSoftShadowSampleCount; ++sampleIndex) {

		float3 samplePos = GetRectLightSamplePosition(
			light, kRectLightSamples[sampleIndex]);
		float3 toLight = samplePos - worldPos;
		float sampleDistance = length(toLight);
		if (sampleDistance <= 1e-5f) {
			continue;
		}

		float3 L = toLight / sampleDistance;
		float sourceFacing =
			saturate(dot(-L, light.direction));
		float3 radiance = light.color.rgb * light.intensity *
			attenuation * barnAttenuation * sourceFacing * shadow;
		result += EvaluatePBRLight(
			N, V, L, radiance, albedo, metallic, roughness, F0);
	}
	return result / float(kMaximumSoftShadowSampleCount);
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

//============================================================================
//	背景、スカイボックス
//============================================================================

float3 SampleBackground(float2 texcoord) {

	// スカイボックスが無効なら処理しない、色をそのまま返す
	if (hasSkybox == 0u || skyboxCubemapIndex == kNoCubemap) {
		return skyboxColor.rgb;
	}

	// 入力テクスチャ座標からNDCを作ってinverseViewProjectionでワールド方向を復元
	float2 ndc = float2(texcoord.x * 2.0f - 1.0f, 1.0f - texcoord.y * 2.0f);
	float4 worldFar = mul(float4(ndc, 1.0f, 1.0f), inverseViewProjection);
	float3 direction = normalize(worldFar.xyz / worldFar.w - cameraPos);

	// キューブマップテクスチャ取得
	TextureCube<float4> cubemap = NEM_TEXTURECUBE(skyboxCubemapIndex);
	return cubemap.SampleLevel(gSampler, direction, 0.0f).rgb * skyboxColor.rgb;
}

bool ProjectScreenSpaceSample(float3 samplePos, out float2 uv,
	out float sampleDepth) {

	float4 clip = mul(float4(samplePos, 1.0f), viewProjectionMatrix);
	uv = 0.0f.xx;
	sampleDepth = 0.0f;
	if (clip.w <= 0.0f) {
		return false;
	}
	uv = float2(
		clip.x / clip.w * 0.5f + 0.5f,
		0.5f - clip.y / clip.w * 0.5f);
	if (any(uv <= 0.0f.xx) || any(1.0f.xx <= uv)) {
		return false;
	}
	sampleDepth = mul(float4(samplePos, 1.0f), viewMatrix).z;
	return true;
}

bool LoadScreenSpaceDepthDelta(float2 uv, float sampleDepth,
	out float depthDelta) {

	uint2 hitPixel = min(uint2(uv * viewportSize), viewportSize - 1u);
	depthDelta = 0.0f;
	if ((gFlags.Load(int3(hitPixel, 0)) & kMaterialFlagSurface) == 0u) {
		return false;
	}
	float3 hitPos = gWorldPos.Load(int3(hitPixel, 0)).xyz;
	float hitDepth = mul(float4(hitPos, 1.0f), viewMatrix).z;
	depthDelta = sampleDepth - hitDepth;
	return true;
}

float3 EvaluateSurfaceLighting(int2 pixel, float3 worldPos,
	float3 N, float3 V, float3 albedo, float metallic,
	float roughness, float ao, float3 emissive, uint flags,
	bool useShadow);

// 粗いMarchで見つけた交差区間を二分探索して縞状の欠落を防ぐ
bool TraceScreenSpaceReflection(float3 worldPos, float3 direction,
	bool useShadow,
	out float3 reflectionColor) {

	reflectionColor = 0.0f.xxx;
	float distance = 0.2f;
	float previousDistance = 0.0f;
	float previousDepthDelta = 0.0f;
	bool previousSurface = false;
	[loop]
	for (uint step = 0u; step < 32u; ++step) {

		float3 samplePos = worldPos + direction * distance;
		float2 uv;
		float sampleDepth;
		if (!ProjectScreenSpaceSample(samplePos, uv, sampleDepth)) {
			return false;
		}
		float depthDelta;
		const bool surface = LoadScreenSpaceDepthDelta(
			uv, sampleDepth, depthDelta);
		if (surface && 0.0f <= depthDelta) {

			float2 hitUV = uv;
			float hitDepthDelta = depthDelta;
			if (previousSurface && previousDepthDelta < 0.0f) {

				float lowerDistance = previousDistance;
				float upperDistance = distance;
				[unroll]
				for (uint refinement = 0u; refinement < 6u; ++refinement) {

					const float middleDistance =
						(lowerDistance + upperDistance) * 0.5f;
					const float3 middlePos =
						worldPos + direction * middleDistance;
					float2 middleUV;
					float middleDepth;
					float middleDepthDelta;
					const bool middleProjected = ProjectScreenSpaceSample(
						middlePos, middleUV, middleDepth);
					const bool middleSurface = middleProjected &&
						LoadScreenSpaceDepthDelta(
							middleUV, middleDepth, middleDepthDelta);
					if (middleSurface && 0.0f <= middleDepthDelta) {

						upperDistance = middleDistance;
						hitUV = middleUV;
						hitDepthDelta = middleDepthDelta;
					} else {

						lowerDistance = middleDistance;
					}
				}
			}

			const float thickness = max(0.08f, distance * 0.015f);
			if (hitDepthDelta <= thickness * 2.0f) {

				int2 hitPixel = min(
					int2(hitUV * viewportSize), int2(viewportSize) - 1);
				uint hitFlags = gFlags.Load(int3(hitPixel, 0));
				float3 hitAlbedo = gAlbedo.Load(int3(hitPixel, 0)).rgb;
				float3 hitNormal = normalize(
					gNormal.Load(int3(hitPixel, 0)).xyz * 2.0f - 1.0f);
				float3 hitWorldPos =
					gWorldPos.Load(int3(hitPixel, 0)).xyz;
				float4 hitMaterial = gMaterial.Load(int3(hitPixel, 0));
				float3 hitEmissive =
					gEmissive.Load(int3(hitPixel, 0)).rgb;
				reflectionColor = EvaluateSurfaceLighting(
					hitPixel, hitWorldPos, hitNormal, normalize(-direction),
					hitAlbedo, hitMaterial.r, max(hitMaterial.g, 0.04f),
					hitMaterial.b, hitEmissive, hitFlags, useShadow);
				return true;
			}
		}
		previousSurface = surface;
		previousDistance = distance;
		previousDepthDelta = depthDelta;
		distance += 0.12f + distance * 0.08f;
	}
	return false;
}

// 非RT環境では画面内のGBufferを使って平行光源の遮蔽を補う
float TraceScreenSpaceShadow(float3 worldPos, float3 direction) {

	float distance = 0.12f;
	[loop]
	for (uint step = 0u; step < 32u; ++step) {

		float3 samplePos = worldPos + direction * distance;
		float4 clip = mul(float4(samplePos, 1.0f), viewProjectionMatrix);
		if (clip.w <= 0.0f) {
			return 0.0f;
		}
		float2 uv = float2(
			clip.x / clip.w * 0.5f + 0.5f,
			0.5f - clip.y / clip.w * 0.5f);
		if (any(uv <= 0.0f.xx) || any(1.0f.xx <= uv)) {
			return 0.0f;
		}

		uint2 hitPixel = min(uint2(uv * viewportSize), viewportSize - 1u);
		if ((gFlags.Load(int3(hitPixel, 0)) & kMaterialFlagSurface) != 0u) {

			float3 hitPos = gWorldPos.Load(int3(hitPixel, 0)).xyz;
			float sampleDepth = mul(float4(samplePos, 1.0f), viewMatrix).z;
			float hitDepth = mul(float4(hitPos, 1.0f), viewMatrix).z;
			float thickness = max(0.08f, distance * 0.02f);
			if (0.0f <= sampleDepth - hitDepth && sampleDepth - hitDepth <= thickness) {
				return 1.0f;
			}
		}
		distance += 0.12f + distance * 0.08f;
	}
	return 0.0f;
}

float LoadDirectionalShadowDepth(uint cascade, int2 pixel) {

	if (cascade == 0u) return gDirectionalShadowMap0.Load(int3(pixel, 0));
	if (cascade == 1u) return gDirectionalShadowMap1.Load(int3(pixel, 0));
	if (cascade == 2u) return gDirectionalShadowMap2.Load(int3(pixel, 0));
	return gDirectionalShadowMap3.Load(int3(pixel, 0));
}

// 指定CascadeのShadow Mapを3x3 PCFで評価
float TraceDirectionalShadowCascade(float3 worldPos, float3 worldNormal,
	uint cascade) {

	float4 shadowClip = mul(float4(worldPos, 1.0f),
		shadowViewProjections[cascade]);
	float3 shadowNDC = shadowClip.xyz / max(shadowClip.w, 1e-5f);
	float2 uv = float2(shadowNDC.x * 0.5f + 0.5f,
		0.5f - shadowNDC.y * 0.5f);
	if (any(uv <= 0.0f.xx) || any(1.0f.xx <= uv) ||
		shadowNDC.z <= 0.0f || 1.0f <= shadowNDC.z) {

		return 0.0f;
	}

	uint width;
	uint height;
	if (cascade == 0u) gDirectionalShadowMap0.GetDimensions(width, height);
	else if (cascade == 1u) gDirectionalShadowMap1.GetDimensions(width, height);
	else if (cascade == 2u) gDirectionalShadowMap2.GetDimensions(width, height);
	else gDirectionalShadowMap3.GetDimensions(width, height);
	int2 center = int2(uv * uint2(width, height));
	float slope = 1.0f - saturate(abs(dot(worldNormal,
		normalize(gDirectionalLights[shadowMapLightIndex].direction))));
	float depthRange = max(shadowDepthRanges[cascade], 1.0f);
	float bias = (0.08f + slope * 0.25f) / depthRange;
	float occlusion = 0.0f;
	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {

			int2 samplePixel = clamp(center + int2(x, y),
				int2(0, 0), int2(width - 1u, height - 1u));
			occlusion += LoadDirectionalShadowDepth(cascade, samplePixel) +
				bias < shadowNDC.z ? 1.0f : 0.0f;
		}
	}
	return occlusion / 9.0f;
}

// Cascade境界を補間してCamera移動時の影の切り替わりを抑える
float TraceDirectionalShadowMap(float3 worldPos, float3 worldNormal) {

	float viewDepth = mul(float4(worldPos, 1.0f), viewMatrix).z;
	uint cascade = viewDepth <= shadowCascadeSplits.x ? 0u :
		(viewDepth <= shadowCascadeSplits.y ? 1u :
			(viewDepth <= shadowCascadeSplits.z ? 2u : 3u));
	float occlusion = TraceDirectionalShadowCascade(
		worldPos, worldNormal, cascade);
	if (cascade >= 3u) {
		return occlusion;
	}

	float cascadeNear = cascade == 0u ? clusterNearClip :
		shadowCascadeSplits[cascade - 1u];
	float cascadeFar = shadowCascadeSplits[cascade];
	float blendWidth = max((cascadeFar - cascadeNear) * 0.10f, 0.001f);
	float blend = saturate((viewDepth - (cascadeFar - blendWidth)) /
		blendWidth);
	if (blend <= 0.0f) {
		return occlusion;
	}
	return lerp(occlusion, TraceDirectionalShadowCascade(
		worldPos, worldNormal, cascade + 1u), blend);
}

// GBufferのMaterialへ通常描画と同じ直接光と拡散環境光を適用
float3 EvaluateSurfaceLighting(int2 pixel, float3 worldPos,
	float3 N, float3 V, float3 albedo, float metallic,
	float roughness, float ao, float3 emissive, uint flags,
	bool useShadow) {

	if ((flags & kMaterialFlagLighting) == 0u) {
		return albedo + emissive;
	}

	float3 F0 = lerp(0.04f.xxx, albedo, metallic);
	float3 color = 0.0f.xxx;
	[loop]
	for (uint index = 0u; index < directionalCount; ++index) {

		DirectionalLight light = gDirectionalLights[index];
		float shadow = 1.0f;
		if (light.shadowStrength > 0.0f &&
			(flags & kMaterialFlagReceiveShadow) != 0u) {

			float occlusion = useShadow ?
				(filteredShadowAvailable != 0u ? LoadFilteredShadow(pixel, index) :
					TraceDirectionalShadow(worldPos, N, light.direction, light.shadowAngularRadius, pixel, index)) :
				(shadowMapAvailable != 0u && index == shadowMapLightIndex ?
					TraceDirectionalShadowMap(worldPos, N) :
					TraceScreenSpaceShadow(
						worldPos + N * shadowNormalBias,
						normalize(-light.direction)));
			shadow = 1.0f - occlusion * light.shadowStrength;
		}
		float3 radiance = light.color.rgb * light.intensity * shadow;
		color += EvaluatePBRLight(N, V, normalize(-light.direction),
			radiance, albedo, metallic, roughness, F0);
	}

	LightClusterHeader clusterHeader;
	if (ResolveLightCluster(pixel, worldPos, clusterHeader)) {

		[loop]
		for (uint clusterLight = 0u;
			clusterLight < clusterHeader.count; ++clusterLight) {

			uint localIndex =
				gLightClusterIndices[clusterHeader.offset + clusterLight];
			if (localIndex < pointCount) {

				color += EvaluatePointLightIndex(localIndex,
					worldPos, N, V, albedo, metallic, roughness,
					F0, flags, useShadow, pixel);
				continue;
			}
			uint spotIndex = localIndex - pointCount;
			if (spotIndex < spotCount) {

				color += EvaluateSpotLightIndex(spotIndex,
					worldPos, N, V, albedo, metallic, roughness,
					F0, flags, useShadow, pixel);
				continue;
			}
			uint rectIndex = spotIndex - spotCount;
			if (rectIndex < rectCount) {

				color += EvaluateRectLightIndex(rectIndex,
					worldPos, N, V, albedo, metallic, roughness,
					F0, flags, useShadow, pixel);
			}
		}
	} else {

		[loop]
		for (uint index = 0u; index < pointCount; ++index) {

			color += EvaluatePointLightIndex(index,
				worldPos, N, V, albedo, metallic, roughness,
				F0, flags, useShadow, pixel);
		}
		[loop]
		for (uint index = 0u; index < spotCount; ++index) {

			color += EvaluateSpotLightIndex(index,
				worldPos, N, V, albedo, metallic, roughness,
				F0, flags, useShadow, pixel);
		}
		[loop]
		for (uint index = 0u; index < rectCount; ++index) {

			color += EvaluateRectLightIndex(index,
				worldPos, N, V, albedo, metallic, roughness,
				F0, flags, useShadow, pixel);
		}
	}

	if ((flags & kMaterialFlagReceiveIBL) != 0u) {

		if (hasSkybox != 0u && irradianceCubemapIndex != kNoCubemap) {

			TextureCube<float4> irradianceMap =
				NEM_TEXTURECUBE(irradianceCubemapIndex);
			float3 irradiance =
				irradianceMap.SampleLevel(gSampler, N, 0.0f).rgb;
			color += irradiance * skyboxColor.rgb * iblIntensity *
				albedo * ao;
		} else {

			color += ambientIntensity * albedo * ao;
		}
	}
	return color + emissive;
}

// SSRが見つからない範囲はSkyboxのReflection Captureで補う
float3 ResolveEnvironmentReflection(float3 worldPos, float3 N, float3 V,
	float roughness, bool useShadow) {

	float3 reflectionDirection = normalize(reflect(-V, N));
	float3 result = 0.0f.xxx;
	if (roughness < 0.85f &&
		TraceScreenSpaceReflection(
			worldPos + N * 0.05f, reflectionDirection,
			useShadow, result)) {

		return result;
	}
	if (hasSkybox == 0u || skyboxCubemapIndex == kNoCubemap) {
		return result;
	}

	TextureCube<float4> cubemap = NEM_TEXTURECUBE(skyboxCubemapIndex);
	uint width;
	uint height;
	uint mipLevels;
	cubemap.GetDimensions(0u, width, height, mipLevels);
	float mip = roughness * max(float(mipLevels) - 1.0f, 0.0f);
	return cubemap.SampleLevel(gSampler, reflectionDirection, mip).rgb *
		skyboxColor.rgb * iblIntensity;
}

//============================================================================
//	全ピクセルのマテリアル計算
//============================================================================
float4 ResolvePixel(VSOutput input, bool useShadow) {

	int3 pixel = int3(input.position.xy, 0);

	// サーフェスが無いピクセルは背景
	uint flags = gFlags.Load(pixel);
	if ((flags & kMaterialFlagSurface) == 0u) {
		return float4(SampleBackground(input.texcoord), 1.0f);
	}
	// GBufferデータ取得
	float3 albedo = gAlbedo.Load(pixel).rgb;
	float3 N = normalize(gNormal.Load(pixel).xyz * 2.0f - 1.0f);
	float3 worldPos = gWorldPos.Load(pixel).xyz;
	float4 material = gMaterial.Load(pixel);
	float metallic = material.r;
	float roughness = max(material.g, 0.04f);
	float ao = material.b;
	float3 emissive = gEmissive.Load(pixel).rgb;

	float3 V = normalize(cameraPos - worldPos);
	float3 F0 = lerp(0.04f.xxx, albedo, metallic);
	// 遮蔽は環境光だけに適用する
	float ambientAO = ambientOcclusionIntensity <= 0.0f ? 1.0f : pow(saturate(gAmbientOcclusion.SampleLevel(gSampler, input.texcoord, 0.0f).r),
		max(ambientOcclusionIntensity, 0.0f));
	float3 color = EvaluateSurfaceLighting(
		pixel.xy, worldPos, N, V, albedo, metallic,
		roughness, ao * ambientAO, emissive, flags, useShadow);

	// 主サーフェイスだけSSRまたはSkyboxの鏡面反射を加える
	if ((flags & kMaterialFlagLighting) != 0u &&
		(flags & kMaterialFlagReceiveIBL) != 0u &&
		reflectionFeatureActive == 0u) {

		float NdotV = saturate(dot(N, V));
		float3 environment = ResolveEnvironmentReflection(
			worldPos, N, V, roughness, useShadow);
		color += environment * FresnelSchlick(NdotV, F0) *
			(1.0f - roughness * 0.75f) * ao * reflectionIntensity;
	}

	return float4(color, 1.0f);
}

//============================================================================
//	main、影無し
//============================================================================
float4 main(VSOutput input) : SV_TARGET0 {

	return ResolvePixel(input, false);
}
//============================================================================
//	mainShadowed、影あり
//============================================================================
float4 mainShadowed(VSOutput input) : SV_TARGET0 {

	return ResolvePixel(input, true);
}
