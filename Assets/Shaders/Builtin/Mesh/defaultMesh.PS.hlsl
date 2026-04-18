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
// インスタンス
struct PSInstance {
	
	float4 testColor;
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
StructuredBuffer<PSInstance> gPSInstances : register(t3);
StructuredBuffer<DirectionalLight> gDirectionalLights : register(t4);
StructuredBuffer<PointLight> gPointLights : register(t5);
StructuredBuffer<SpotLight> gSpotLights : register(t6);
StructuredBuffer<TileLightGridEntry> gTileLightGrid : register(t7);
StructuredBuffer<uint> gTileLightIndexList : register(t8);

SamplerState gSampler : register(s0);

//============================================================================
//	functions
//============================================================================

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
// 距離減衰
float ComputeDistanceAttenuation(float distanceValue, float rangeValue, float decayValue) {

	if (rangeValue <= 1e-5f) {
		return 0.0f;
	}
	if (distanceValue >= rangeValue) {
		return 0.0f;
	}

	float t = saturate(1.0f - distanceValue / rangeValue);
	return pow(t, max(decayValue, 1e-4f));
}
// 平行光源計算
float3 EvaluateDirectionalLight(DirectionalLight light, float3 worldNormal) {

	float3 L = normalize(-light.direction);
	float ndotl = saturate(dot(worldNormal, L));

	return light.color.rgb * (light.intensity * ndotl);
}
// 点光源計算
float3 EvaluatePointLight(PointLight light, float3 worldPos, float3 worldNormal) {

	float3 toLight = light.pos - worldPos;
	float distanceValue = length(toLight);
	if (distanceValue <= 1e-5f) {
		return 0.0f.xxx;
	}

	float3 L = toLight / distanceValue;
	float ndotl = saturate(dot(worldNormal, L));
	float attenuation = ComputeDistanceAttenuation(distanceValue, light.radius, light.decay);

	return light.color.rgb * (light.intensity * ndotl * attenuation);
}
// スポットライト計算
float3 EvaluateSpotLight(SpotLight light, float3 worldPos, float3 worldNormal) {

	float3 toLight = light.pos - worldPos;
	float distanceValue = length(toLight);
	if (distanceValue <= 1e-5f) {
		return 0.0f.xxx;
	}

	float3 L = toLight / distanceValue;
	float ndotl = saturate(dot(worldNormal, L));
	if (ndotl <= 0.0f) {
		return 0.0f.xxx;
	}

	float distanceAttenuation = ComputeDistanceAttenuation(distanceValue, light.distance, light.decay);
	if (distanceAttenuation <= 0.0f) {
		return 0.0f.xxx;
	}

	float3 lightDir = normalize(light.direction);
	float cosTheta = dot(-L, lightDir);

	float coneRange = max(light.cosFalloffStart - light.cosAngle, 1e-4f);
	float coneAttenuation = saturate((cosTheta - light.cosAngle) / coneRange);

	// 少しだけ柔らかくする
	coneAttenuation *= coneAttenuation;

	return light.color.rgb * (light.intensity * ndotl * distanceAttenuation * coneAttenuation);
}

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	// インスタンスからサブメッシュごとのデータを取得
	SubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);

	PSOutput output;

	// UV変換
	float4 transformUV = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix);

	// アルベド
	Texture2D<float4> albedoTexture = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.baseColorTextureIndex)];
	float4 albedoColor = albedoTexture.Sample(gSampler, transformUV.xy);

	// テクスチャのアルベド、インポートされたアルベド、サブメッシュカラーを乗算
	albedoColor *= subMesh.importedBaseColor;
	albedoColor *= subMesh.color;
	
	// 法線
	float3 worldNormal = normalize(input.normal);

	// 環境光
	float3 lighting = 0.15f.xxx;

	//============================================================================
	//	平行光源
	//============================================================================
	[loop]
	for (uint i = 0; i < directionalCount; ++i) {

		lighting += EvaluateDirectionalLight(gDirectionalLights[i], worldNormal);
	}

	if (0 < localCount && 0 < maxLocalLightsPerTile) {

		uint tileIndex = ComputeTileIndex(input.position);
		TileLightGridEntry grid = gTileLightGrid[tileIndex];
		uint loopCount = min(grid.count, maxLocalLightsPerTile);
		[loop]
		for (uint i = 0; i < loopCount; ++i) {

			uint localLightIndex = gTileLightIndexList[grid.offset + i];

			//============================================================================
			//	点光源
			//============================================================================
			if (localLightIndex < pointCount) {
				
				lighting += EvaluatePointLight(gPointLights[localLightIndex], input.worldPos, worldNormal);
			}
			//============================================================================
			//	スポットライト
			//============================================================================
			else {

				uint spotIndex = localLightIndex - pointCount;
				if (spotIndex < spotCount) {
					
					lighting += EvaluateSpotLight(gSpotLights[spotIndex], input.worldPos, worldNormal);
				}
			}
		}
	}

	// 出力設定
	output.color = float4(albedoColor.rgb * lighting, albedoColor.a); 
	// ワールド法線
	output.normal = float4(worldNormal * 0.5f + 0.5f, 1.0f);
	// ワールド座標
	output.worldPos = float4(input.worldPos, 1.0f);
	return output;
}