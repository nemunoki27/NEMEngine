//============================================================================
//	include
//============================================================================
#include "../Common/sceneViewDepth.hlsli"
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
Texture2D<float4> gDiffuseGI : register(t20);
Texture2D<float4> gAmbientOcclusion : register(t17);
StructuredBuffer<uint> gFilteredRaytracedShadows : register(t18);
StructuredBuffer<uint> gShadowLightIndices : register(t19);

#include "restirDIReservoir.hlsli"
StructuredBuffer<uint2> gReSTIRRadiance : register(t21);

SamplerState gSampler : register(s0);

//============================================================================
//	ライト
//============================================================================
#include "lightShaderData.hlsli"
#include "lightAttenuation.hlsli"
#include "rectAreaLight.hlsli"

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
	uint diffuseGIMode;

	float4x4 shadowViewProjections[4];
	float4 shadowCascadeSplits;
	float4 shadowDepthRanges;
	uint environmentSpecularIndex;
	uint environmentBRDFIndex;
	uint physicalEnvironmentEnabled;
	uint restirDIAvailable;
	float4 atmosphereSun;
	float4 atmosphereSolar;
	float4 atmosphereMoon;
	float4 atmosphereLunar;
	uint atmosphereSkyIndex;
	uint3 _atmospherePad;
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
	if (slot == 0xFFFFFFFFu) return -1.0f;
	return float(gFilteredRaytracedShadows[(slot * viewportSize.y + pixel.y) * viewportSize.x + pixel.x] & 65535u) / 65535.0f;
}


#include "localDirectLighting.hlsli"

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
	float4 worldFar = mul(float4(ndc, kSceneFarDepth, 1.0f), inverseViewProjection);
	float3 direction = normalize(worldFar.xyz / worldFar.w - cameraPos);
	if (atmosphereSkyIndex != kNoCubemap) {
		float latitude = asin(clamp(direction.y, -1.0f, 1.0f)) / 1.570796327f;
		float2 skyUV = float2(frac(atan2(direction.z, direction.x) / 6.283185307f + 1.0f),
			0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)));
		Texture2D<float4> sky = NEM_TEXTURE2D(atmosphereSkyIndex);
		float3 result = sky.SampleLevel(gSampler, skyUV, 0).rgb;
		float angle = acos(clamp(dot(direction, atmosphereSun.xyz), -1.0f, 1.0f));
		// 背景と物体の分岐に依存せず隣接画素の角度幅を求める
		float2 pixelStep = 2.0f / max(float2(viewportSize), 1.0f.xx);
		float4 adjacentX = worldFar + inverseViewProjection[0] * pixelStep.x;
		float4 adjacentY = worldFar - inverseViewProjection[1] * pixelStep.y;
		float3 directionX = normalize(adjacentX.xyz / adjacentX.w - cameraPos);
		float3 directionY = normalize(adjacentY.xyz / adjacentY.w - cameraPos);
		float pixelWidth = max(max(length(directionX - direction), length(directionY - direction)), 1e-6f);
		float coverage = 1.0f - smoothstep(max(atmosphereSun.w - pixelWidth * 0.5f, 0.0f),
			atmosphereSun.w + pixelWidth * 0.5f, angle);
		float moonAngle = acos(clamp(dot(direction, atmosphereMoon.xyz), -1.0f, 1.0f));
		float moonCoverage = 1.0f - smoothstep(max(atmosphereMoon.w - pixelWidth * 0.5f, 0.0f),
			atmosphereMoon.w + pixelWidth * 0.5f, moonAngle);
		return min(result + atmosphereSolar.rgb * coverage + atmosphereLunar.rgb * moonCoverage, 65000.0f);
	}

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
	float roughness, float ao, float screenAO, float3 emissive, uint flags,
	bool useShadow, bool primarySurface);

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
					hitMaterial.b, 1.0f, hitEmissive, hitFlags, useShadow, false);
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

// Skyboxの放射照度から拡散環境光を計算
float3 EvaluateDiffuseEnvironment(float3 N, float3 V, float3 albedo, float metallic, float ao, float3 F0) {

	float3 color = 0.0f;
	if (hasSkybox != 0u && irradianceCubemapIndex != kNoCubemap) {

		TextureCube<float4> irradianceMap = NEM_TEXTURECUBE(irradianceCubemapIndex);
		float3 irradiance = irradianceMap.SampleLevel(gSampler, N, 0.0f).rgb;
		// 金属の拡散光と反射分を重複加算しない
		float3 diffuseWeight = physicalEnvironmentEnabled != 0u ?
			(1.0f - FresnelSchlick(saturate(dot(N, V)), F0)) * (1.0f - metallic) : 1.0f.xxx;
		color += irradiance * skyboxColor.rgb * iblIntensity * albedo * ao * diffuseWeight;
	} else {

		color += ambientIntensity * albedo * ao;
	}
	return color;
}

// GBufferのMaterialへ通常描画と同じ直接光と拡散環境光を適用
float3 EvaluateSurfaceLighting(int2 pixel, float3 worldPos,
	float3 N, float3 V, float3 albedo, float metallic,
	float roughness, float ao, float screenAO, float3 emissive, uint flags,
	bool useShadow, bool primarySurface) {

	if ((flags & kMaterialFlagLighting) == 0u) {
		return albedo + emissive;
	}

	float3 F0 = lerp(0.04f.xxx, albedo, metallic);
	float3 color = 0.0f.xxx;
	[loop]
	for (uint index = 0u; index < directionalCount; ++index) {

		DirectionalLight light = gDirectionalLights[index];
		if (!IsLightRenderingLayerEnabled(light.renderingLayerMask, flags >> 8u)) continue;
		float shadow = 1.0f;
		if (light.shadowStrength > 0.0f &&
			(flags & kMaterialFlagReceiveShadow) != 0u) {

			float occlusion;
			if (useShadow) {
				occlusion = filteredShadowAvailable != 0u ? LoadFilteredShadow(pixel, index) : -1.0f;
				if (occlusion < 0.0f) occlusion = TraceDirectionalShadow(worldPos, N, light.direction, light.shadowAngularRadius, pixel, index, light.shadowFilterIndex);
			} else {
				occlusion = shadowMapAvailable != 0u && index == shadowMapLightIndex ? TraceDirectionalShadowMap(worldPos, N) :
					(light.shadowFilterIndex == 0xFFFFFFFFu ? TraceScreenSpaceShadow(worldPos + N * shadowNormalBias, normalize(-light.direction)) : 0.0f);
			}
			shadow = 1.0f - occlusion * light.shadowStrength;
		}
		float3 radiance = light.color.rgb * light.intensity * shadow;
		color += EvaluatePBRLight(N, V, normalize(-light.direction),
			radiance, albedo, metallic, roughness, F0);
	}

	if (primarySurface && restirDIAvailable != 0u) {
		color += UnpackReSTIRRadiance(gReSTIRRadiance[pixel.y * viewportSize.x + pixel.x]).rgb;
	} else {
		LightClusterHeader clusterHeader;
		bool clustered = ResolveLightCluster(pixel, worldPos, clusterHeader);
		uint count = clustered ? clusterHeader.count : localCount;
		[loop]
		for (uint i = 0; i < count; ++i) {
			uint lightIndex = clustered ? gLightClusterIndices[clusterHeader.offset + i] : i;
			color += EvaluateLocalLight(lightIndex, worldPos, N, V, albedo, metallic,
				roughness, F0, flags, useShadow, pixel);
		}
	}

	if ((flags & kMaterialFlagReceiveIBL) != 0u) {

		float4 gi = float4(0, 0, 0, -1);
		if (diffuseGIMode != 0u) gi = gDiffuseGI.Load(int3(pixel, 0));
		if (gi.a >= 0.0f) {
			color += gi.rgb * DiffuseGIReflectance(albedo, metallic) * (ao * screenAO);
		} else {
			color += EvaluateDiffuseEnvironment(N, V, albedo, metallic, ao * screenAO, F0);
		}
	}
	return color + emissive;
}

// SSRが見つからない範囲はSkyboxの反射で補う
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
		return 0.0f.xxx;
	}

	uint cubemapIndex = environmentSpecularIndex != kNoCubemap ? environmentSpecularIndex : skyboxCubemapIndex;
	TextureCube<float4> cubemap = NEM_TEXTURECUBE(cubemapIndex);
	uint width;
	uint height;
	uint mipLevels;
	cubemap.GetDimensions(0u, width, height, mipLevels);
	float mip = roughness * max(float(mipLevels) - 1.0f, 0.0f);
	float3 skyReflection = cubemap.SampleLevel(gSampler, reflectionDirection, mip).rgb * skyboxColor.rgb * iblIntensity;
	return skyReflection;
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
		roughness, ao, ambientAO, emissive, flags, useShadow, true);

	// 主サーフェイスだけSSRまたはSkyboxの鏡面反射を加える
	if ((flags & kMaterialFlagLighting) != 0u &&
		(flags & kMaterialFlagReceiveIBL) != 0u &&
		reflectionFeatureActive == 0u) {

		float NdotV = saturate(dot(N, V));
		float3 environment = ResolveEnvironmentReflection(
			worldPos, N, V, roughness, useShadow);
		float3 reflectionWeight = FresnelSchlick(NdotV, F0) * (1.0f - roughness * 0.75f);
		if (physicalEnvironmentEnabled != 0u && environmentBRDFIndex != kNoCubemap) {
			Texture2D<float4> brdf = NEM_TEXTURE2D(environmentBRDFIndex);
			float2 integral = brdf.SampleLevel(gSampler, float2(NdotV, roughness), 0.0f).rg;
			reflectionWeight = F0 * integral.x + integral.y;
		}
		color += environment * reflectionWeight * ao * reflectionIntensity;
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
