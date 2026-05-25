//============================================================================
//	include
//============================================================================

#include "defaultMesh.hlsli"

//============================================================================
//	output
//============================================================================

struct PSOutput {

	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
	float4 worldPos : SV_TARGET2;
};

//============================================================================
//	resources
//============================================================================

// ライト
cbuffer LightCounts : register(b2) {

	uint directionalCount;
	uint pointCount;
	uint spotCount;
	uint localCount;
};
cbuffer LightCullingParams : register(b3) {

	float4x4 viewMatrix;
	float4x4 projectionMatrix;
	float4x4 inverseProjectionMatrix;

	uint screenWidth;
	uint screenHeight;
	uint tileSizeX;
	uint tileSizeY;

	uint tileCountX;
	uint tileCountY;
	uint totalTileCount;
	uint maxLocalLightsPerTile;

	uint pointLightCountForCull;
	uint spotLightCountForCull;
	uint localLightCountForCull;
	uint _pad2;

	float nearClip;
	float farClip;
	float2 _pad3;
};
// ライト
struct DirectionalLight {

	float4 color;

	float3 direction;
	float intensity;
};
struct PointLight {

	float4 color;

	float3 pos;
	float intensity;

	float radius;
	float decay;
	float2 _pad0;
};
struct SpotLight {

	float4 color;

	float3 direction;
	float intensity;

	float3 pos;
	float distance;

	float decay;
	float cosAngle;
	float cosFalloffStart;
	float _pad0;
};
struct TileLightGridEntry {

	uint offset;
	uint count;
	uint pointCount;
	uint spotCount;
};
StructuredBuffer<DirectionalLight> gDirectionalLights : register(t4);
StructuredBuffer<PointLight> gPointLights : register(t5);
StructuredBuffer<SpotLight> gSpotLights : register(t6);
StructuredBuffer<TileLightGridEntry> gTileLightGrid : register(t7);
StructuredBuffer<uint> gTileLightIndexList : register(t8);

SamplerState gSampler : register(s0);

//============================================================================
//	定数
//============================================================================

static const float PI = 3.14159265f;
static const uint kNoTexture = 0xFFFFFFFF;

//============================================================================
//	PBR関数
//============================================================================

// GGX分布関数
float EvalD(float NdotH, float roughness) {

	float a = roughness * roughness;
	float a2 = a * a;
	float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
	return a2 / max(PI * denom * denom, 1e-7f);
}

// Smith幾何減衰 (GGX)
float EvalG1(float NdotX, float roughness) {

	float r = roughness + 1.0f;
	float k = (r * r) / 8.0f;
	return NdotX / max(NdotX * (1.0f - k) + k, 1e-7f);
}

float EvalG(float NdotV, float NdotL, float roughness) {

	return EvalG1(NdotV, roughness) * EvalG1(NdotL, roughness);
}

// Fresnel-Schlick近似
float3 FresnelSchlick(float cosTheta, float3 F0) {

	return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// ディズニーベース拡散反射
float3 DisneyDiffuse(float NdotL, float NdotV, float LdotH, float roughness, float3 albedo) {

	float energyBias = lerp(0.0f, 0.5f, roughness);
	float energyFactor = lerp(1.0f, 1.0f / 1.51f, roughness);
	float Fd90 = energyBias + 2.0f * LdotH * LdotH * roughness;
	float FL = 1.0f + (Fd90 - 1.0f) * pow(1.0f - NdotL, 5.0f);
	float FV = 1.0f + (Fd90 - 1.0f) * pow(1.0f - NdotV, 5.0f);
	return albedo * FL * FV * energyFactor / PI;
}

// タイルインデックス計算
uint ComputeTileIndex(float4 svPosition) {

	uint safeTileSizeX = max(tileSizeX, 1u);
	uint safeTileSizeY = max(tileSizeY, 1u);

	uint2 tileCoord;
	tileCoord.x = (uint) svPosition.x / safeTileSizeX;
	tileCoord.y = (uint) svPosition.y / safeTileSizeY;

	uint safeTileCountX = max(tileCountX, 1u);
	uint safeTileCountY = max(tileCountY, 1u);

	tileCoord.x = min(tileCoord.x, safeTileCountX - 1u);
	tileCoord.y = min(tileCoord.y, safeTileCountY - 1u);

	return tileCoord.y * safeTileCountX + tileCoord.x;
}

// 距離減衰 (逆二乗則 + スムースウィンドウ)
float ComputeDistanceAttenuation(float dist, float range) {

	if (range <= 1e-5f || dist >= range) {
		return 0.0f;
	}
	float distSqr = max(dist * dist, 0.0001f);
	float rangeSqr = range * range;
	float factor = distSqr / rangeSqr;
	float window = saturate(1.0f - factor * factor);
	return (window * window) / distSqr;
}

// ワールド法線の計算(法線マップを考慮)
float3 ComputeWorldNormal(VSOutput input, SubMeshShaderData subMesh, float2 uv) {

	float3 N = normalize(input.normal);
	if (subMesh.normalTextureIndex == kNoTexture) {
		return N;
	}

	// Gram-Schmidtで接線を再直交化
	float3 T = normalize(input.tangent - dot(input.tangent, N) * N);
	float3 B = cross(N, T);

	Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.normalTextureIndex)];
	float3 tangentNormal = normalTex.Sample(gSampler, uv).xyz * 2.0f - 1.0f;

	float3x3 TBN = float3x3(T, B, N);
	return normalize(mul(tangentNormal, TBN));
}

// PBR平行光源
float3 EvaluatePBRDirectionalLight(DirectionalLight light, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0) {

	float3 L = normalize(-light.direction);
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

	return (diffuse + specular) * NdotL * light.color.rgb * light.intensity;
}

// PBR点光源
float3 EvaluatePBRPointLight(PointLight light, float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0) {

	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f) {
		return 0.0f.xxx;
	}

	float3 L = toLight / dist;
	float NdotL = saturate(dot(N, L));
	if (NdotL <= 0.0f) {
		return 0.0f.xxx;
	}

	float attenuation = ComputeDistanceAttenuation(dist, light.radius);
	if (attenuation <= 0.0f) {
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

	return (diffuse + specular) * NdotL * light.color.rgb * light.intensity * attenuation;
}

// PBRスポットライト
float3 EvaluatePBRSpotLight(SpotLight light, float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0) {

	float3 toLight = light.pos - worldPos;
	float dist = length(toLight);
	if (dist <= 1e-5f) {
		return 0.0f.xxx;
	}

	float3 L = toLight / dist;
	float NdotL = saturate(dot(N, L));
	if (NdotL <= 0.0f) {
		return 0.0f.xxx;
	}

	float distanceAttenuation = ComputeDistanceAttenuation(dist, light.distance);
	if (distanceAttenuation <= 0.0f) {
		return 0.0f.xxx;
	}

	float3 lightDir = normalize(light.direction);
	float cosTheta = dot(-L, lightDir);
	float coneRange = max(light.cosFalloffStart - light.cosAngle, 1e-4f);
	float coneAttenuation = saturate((cosTheta - light.cosAngle) / coneRange);
	coneAttenuation *= coneAttenuation;
	if (coneAttenuation <= 0.0f) {
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

	return (diffuse + specular) * NdotL * light.color.rgb * light.intensity * distanceAttenuation * coneAttenuation;
}

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	SubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);

	PSOutput output;

	// UV変換
	float2 uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;

	// ベースカラー
	Texture2D<float4> baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.baseColorTextureIndex)];
	float4 baseColor = baseColorTex.Sample(gSampler, uv);
	baseColor *= subMesh.importedBaseColor;
	baseColor *= subMesh.color;

	// メタリック/ラフネス (R=metallic, G=roughness)
	float metallic = subMesh.metallic;
	float roughness = subMesh.roughness;
	if (subMesh.metallicRoughnessTextureIndex != kNoTexture) {

		Texture2D<float4> mrTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.metallicRoughnessTextureIndex)];
		float4 mrSample = mrTex.Sample(gSampler, uv);
		metallic = mrSample.r;
		roughness = mrSample.g;
	}
	roughness = max(roughness, 0.04f);

	// AO
	float ao = 1.0f;
	if (subMesh.occlusionTextureIndex != kNoTexture) {

		Texture2D<float4> aoTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.occlusionTextureIndex)];
		ao = aoTex.Sample(gSampler, uv).r;
	}

	// ワールド法線
	float3 N = ComputeWorldNormal(input, subMesh, uv);

	// 視線ベクトル
	float3 V = normalize(cullingCameraPos - input.worldPos);

	// Fresnel F0 (金属は albedo、非金属は0.04)
	float3 F0 = lerp(0.04f.xxx, baseColor.rgb, metallic);

	// スペキュラテクスチャによるF0上書き
	if (subMesh.specularTextureIndex != kNoTexture) {

		Texture2D<float4> specTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.specularTextureIndex)];
		float3 specColor = specTex.Sample(gSampler, uv).rgb;
		F0 = lerp(specColor, baseColor.rgb, metallic);
	}

	//============================================================================
	//	ライティング
	//============================================================================
	float3 Lo = 0.0f.xxx;

	[loop]
	for (uint i = 0; i < directionalCount; ++i) {

		Lo += EvaluatePBRDirectionalLight(gDirectionalLights[i], N, V,
			baseColor.rgb, metallic, roughness, F0);
	}

	if (0 < localCount && 0 < maxLocalLightsPerTile) {

		uint tileIndex = ComputeTileIndex(input.position);
		TileLightGridEntry grid = gTileLightGrid[tileIndex];
		uint loopCount = min(grid.count, maxLocalLightsPerTile);
		[loop]
		for (uint i = 0; i < loopCount; ++i) {

			uint localLightIndex = gTileLightIndexList[grid.offset + i];

			if (localLightIndex < pointCount) {

				Lo += EvaluatePBRPointLight(gPointLights[localLightIndex], input.worldPos, N, V,
					baseColor.rgb, metallic, roughness, F0);
			} else {

				uint spotIndex = localLightIndex - pointCount;
				if (spotIndex < spotCount) {

					Lo += EvaluatePBRSpotLight(gSpotLights[spotIndex], input.worldPos, N, V,
						baseColor.rgb, metallic, roughness, F0);
				}
			}
		}
	}

	// 環境光 (AOで減衰)
	float3 ambient = 0.03f * baseColor.rgb * ao;

	// 発光
	float3 emissive = subMesh.emissiveColor.rgb;
	if (subMesh.emissiveTextureIndex != kNoTexture) {

		Texture2D<float4> emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.emissiveTextureIndex)];
		emissive *= emissiveTex.Sample(gSampler, uv).rgb;
	}

	float3 finalColor = Lo + ambient + emissive;

	output.color = float4(finalColor, baseColor.a);
	output.normal = float4(N * 0.5f + 0.5f, 1.0f);
	output.worldPos = float4(input.worldPos, 1.0f);
	return output;
}
