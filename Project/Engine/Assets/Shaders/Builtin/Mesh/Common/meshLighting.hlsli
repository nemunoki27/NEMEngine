#ifndef NEM_MESH_LIGHTING_HLSLI
#define NEM_MESH_LIGHTING_HLSLI

//============================================================================
//	Mesh描画のライティング共通定義
//	平行光源と点光源とスポットライトのバッファや減衰やタイルカリングや法線計算を集約する
//	defaultMesh.hlsliをincludeしてから読むこと、culling系cbufferやBuildMeshTBNに依存する
//	meshPBRとdefaultMeshの両PSで共有する
//============================================================================

//============================================================================
//	定数
//============================================================================
static const float PI = 3.14159265f;
static const uint kNoTexture = 0xFFFFFFFF;
static const uint kLightCullingModeDisabled = 0u;
static const uint kLightCullingModeTile2D = 1u;
static const uint kLightCullingModeClustered = 2u;
static const uint kLightCullingModeDebugAllLightsPerCluster = 3u;

//============================================================================
//	ライトのcbuffer / struct/ resource
//============================================================================
cbuffer LightCounts : register(b2) {

	uint directionalCount;
	uint pointCount;
	uint spotCount;
	uint localCount;
};
cbuffer LightCullingParams : register(b3) {

	float4x4 viewMatrix;
	float4x4 projectionMatrix;

	uint screenWidth;
	uint screenHeight;
	uint tileSizeX;
	uint tileSizeY;

	uint tileCountX;
	uint tileCountY;
	uint totalTileCount;
	uint maxLocalLightsPerTile;

	uint clusterCountZ;
	uint totalClusterCount;
	uint maxLocalLightsPerCluster;
	uint lightCullingMode;

	uint pointLightCountForCull;
	uint spotLightCountForCull;
	uint localLightCountForCull;
	uint lightCullingEnabled;

	float nearClip;
	float farClip;
	float2 _pad3;
};
struct DirectionalLight {

	float4 color;

	float3 direction;
	float intensity;

	float shadowStrength;
	float3 _pad1;
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
//	タイルやクラスタのインデックス計算、カリング用ビューを使う
//============================================================================
uint ComputeTileIndex(float3 worldPos) {

	uint safeTileSizeX = max(tileSizeX, 1u);
	uint safeTileSizeY = max(tileSizeY, 1u);
	float4 clipPos = mul(float4(worldPos, 1.0f), cullingViewProjection);
	float safeW = max(abs(clipPos.w), 1e-5f);
	float2 ndc = clipPos.xy / safeW;
	float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
	float2 pixel = uv * cullingViewSize;

	uint2 tileCoord;
	tileCoord.x = (uint) floor(max(pixel.x, 0.0f)) / safeTileSizeX;
	tileCoord.y = (uint) floor(max(pixel.y, 0.0f)) / safeTileSizeY;

	uint safeTileCountX = max(tileCountX, 1u);
	uint safeTileCountY = max(tileCountY, 1u);

	tileCoord.x = min(tileCoord.x, safeTileCountX - 1u);
	tileCoord.y = min(tileCoord.y, safeTileCountY - 1u);

	return tileCoord.y * safeTileCountX + tileCoord.x;
}

uint ComputeClusterZ(float3 worldPos) {

	uint safeClusterCountZ = max(clusterCountZ, 1u);
	float3 viewPos = mul(float4(worldPos, 1.0f), cullingView).xyz;
	float safeNear = max(nearClip, 1e-4f);
	float safeFar = max(farClip, safeNear + 1e-3f);
	float z01 = saturate((viewPos.z - safeNear) / (safeFar - safeNear));
	return min((uint) floor(z01 * (float) safeClusterCountZ), safeClusterCountZ - 1u);
}

uint ComputeClusterIndex(float3 worldPos) {

	uint tileIndex = ComputeTileIndex(worldPos);
	uint safeTileCountX = max(tileCountX, 1u);
	uint safeTileCountY = max(tileCountY, 1u);

	uint tileCoordY = tileIndex / safeTileCountX;
	uint tileCoordX = tileIndex - tileCoordY * safeTileCountX;
	uint clusterZ = ComputeClusterZ(worldPos);
	return (clusterZ * safeTileCountY + tileCoordY) * safeTileCountX + tileCoordX;
}

//============================================================================
//	距離減衰
//============================================================================
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

//============================================================================
//	ワールド法線の計算、法線マップを考慮する
//============================================================================
float3 ComputeWorldNormal(VSOutput input, uint normalTextureIndex, float2 uv) {

	float3 N = normalize(input.normal);
	if (normalTextureIndex == kNoTexture) {
		return N;
	}

	// TBN構築は共通helperへ集約している、tangentSignとorientationSign補正込み
	float3x3 TBN = BuildMeshTBN(input);

	Texture2D<float4> normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(normalTextureIndex)];
	float3 tangentNormal = normalTex.Sample(gSampler, uv).xyz * 2.0f - 1.0f;

	return normalize(mul(tangentNormal, TBN));
}

#endif // NEM_MESH_LIGHTING_HLSLI
