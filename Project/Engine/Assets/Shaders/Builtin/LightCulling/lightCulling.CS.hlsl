//============================================================================
//	resources
//============================================================================

cbuffer LightCullingParams : register(b0) {

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

	uint cullPointLightCount;
	uint cullSpotLightCount;
	uint cullLocalLightCount;
	uint lightCullingEnabled;

	float nearClip;
	float farClip;
	float2 _pad1;
};
struct PointLight {

	float4 color;

	float3 pos;
	float intensity;

	float radius;
	float decay;
	float2 _pad;
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
	float _pad;
};
struct TileLightGridEntry {
	
	uint offset;
	uint count;
	uint pointCount;
	uint spotCount;
};

StructuredBuffer<PointLight> gPointLights : register(t2);
StructuredBuffer<SpotLight> gSpotLights : register(t3);
RWStructuredBuffer<TileLightGridEntry> gTileLightGrid : register(u1);
RWStructuredBuffer<uint> gTileLightIndexList : register(u2);

Texture2D<float4> gSourceColor : register(t0);
Texture2D<float> gSourceDepth : register(t1);

//============================================================================
//	groupshared
//============================================================================

groupshared uint gTileMinDepthBits;
groupshared uint gTileMaxDepthBits;
groupshared uint gTileValidDepthCount;

//============================================================================
//	functions
//============================================================================

bool ComputeSphereTileBounds(float3 worldPos, float radius, out uint2 outMinTile, out uint2 outMaxTile) {

	float4 viewPos4 = mul(float4(worldPos, 1.0f), viewMatrix);
	if (viewPos4.z <= 1e-4f) {
		outMinTile = uint2(0, 0);
		outMaxTile = uint2(0, 0);
		return false;
	}

	float4 clipPos = mul(viewPos4, projectionMatrix);
	if (clipPos.w <= 1e-4f) {
		outMinTile = uint2(0, 0);
		outMaxTile = uint2(0, 0);
		return false;
	}

	float2 ndcCenter = clipPos.xy / clipPos.w;

	float projX = abs(projectionMatrix[0][0]);
	float projY = abs(projectionMatrix[1][1]);

	float safeViewZ = max(viewPos4.z, 1e-4f);
	float2 ndcRadius = float2(projX * radius / safeViewZ, projY * radius / safeViewZ);

	float2 uvCenter = float2(ndcCenter.x * 0.5f + 0.5f, -ndcCenter.y * 0.5f + 0.5f);
	float2 uvRadius = ndcRadius * 0.5f;

	float2 pixelMin = (uvCenter - uvRadius) * float2(screenWidth, screenHeight);
	float2 pixelMax = (uvCenter + uvRadius) * float2(screenWidth, screenHeight);

	pixelMin = clamp(pixelMin, float2(0.0f, 0.0f), float2(screenWidth - 1.0f, screenHeight - 1.0f));
	pixelMax = clamp(pixelMax, float2(0.0f, 0.0f), float2(screenWidth - 1.0f, screenHeight - 1.0f));

	outMinTile = uint2((uint) floor(pixelMin.x / tileSizeX), (uint) floor(pixelMin.y / tileSizeY));
	outMaxTile = uint2((uint) floor(pixelMax.x / tileSizeX), (uint) floor(pixelMax.y / tileSizeY));

	outMinTile = clamp(outMinTile, uint2(0, 0), uint2(tileCountX - 1, tileCountY - 1));
	outMaxTile = clamp(outMaxTile, uint2(0, 0), uint2(tileCountX - 1, tileCountY - 1));
	return true;
}

bool TileContainsLight(uint2 tileCoord, uint2 minTile, uint2 maxTile) {

	return (minTile.x <= tileCoord.x && tileCoord.x <= maxTile.x &&
		    minTile.y <= tileCoord.y && tileCoord.y <= maxTile.y);
}

float ReconstructViewZ(float deviceDepth) {

	float4 clipPos = float4(0.0f, 0.0f, deviceDepth, 1.0f);
	float4 viewPos = mul(clipPos, inverseProjectionMatrix);

	float safeW = max(viewPos.w, 1e-5f);
	return viewPos.z / safeW;
}

bool OverlapsDepthRange(float lightViewZ, float lightRadius, float tileMinViewZ, float tileMaxViewZ) {

	float lightMinZ = max(lightViewZ - lightRadius, nearClip);
	float lightMaxZ = lightViewZ + lightRadius;

	return !(lightMaxZ < tileMinViewZ || tileMaxViewZ < lightMinZ);
}

//============================================================================
//	main
//============================================================================
[numthreads(16, 16, 1)]
// カリングなし、そのままライトデータを送る
//void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {

//	if (groupID.x >= tileCountX || groupID.y >= tileCountY) {
//		return;
//	}

//	const uint localThreadIndex = groupThreadID.y * 16u + groupThreadID.x;

//	// 1タイルにつき1スレッドだけがライトリストを書く
//	if (localThreadIndex != 0) {
//		return;
//	}

//	const uint tileIndex = groupID.y * tileCountX + groupID.x;

//	TileLightGridEntry grid;
//	grid.offset = tileIndex * maxLocalLightsPerTile;
//	grid.count = 0;
//	grid.pointCount = 0;
//	grid.spotCount = 0;

//	// maxLocalLightsPerTile が 0 なら何も登録できない
//	if (maxLocalLightsPerTile == 0) {
//		gTileLightGrid[tileIndex] = grid;
//		return;
//	}

//	// PIXで見やすいように、未使用スロットを一旦 0xFFFFFFFF で埋める
//	for (uint i = 0; i < maxLocalLightsPerTile; ++i) {
//		gTileLightIndexList[grid.offset + i] = 0xFFFFFFFFu;
//	}

//	//========================================================================
//	// point lights: 0 ～ cullPointLightCount - 1
//	//========================================================================
//	for (uint pointIndex = 0; pointIndex < cullPointLightCount; ++pointIndex) {

//		if (grid.count >= maxLocalLightsPerTile) {
//			break;
//		}

//		gTileLightIndexList[grid.offset + grid.count] = pointIndex;

//		grid.count++;
//		grid.pointCount++;
//	}

//	//========================================================================
//	// spot lights: cullPointLightCount ～ cullPointLightCount + cullSpotLightCount - 1
//	//========================================================================
//	for (uint spotIndex = 0; spotIndex < cullSpotLightCount; ++spotIndex) {

//		if (grid.count >= maxLocalLightsPerTile) {
//			break;
//		}

//		gTileLightIndexList[grid.offset + grid.count] = cullPointLightCount + spotIndex;

//		grid.count++;
//		grid.spotCount++;
//	}

//	gTileLightGrid[tileIndex] = grid;
//}
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {

	if (groupID.x >= tileCountX || groupID.y >= tileCountY) {
		return;
	}

	const uint localThreadIndex = groupThreadID.y * 16u + groupThreadID.x;

	// 1タイルにつき1スレッドだけがライトリストを書く
	if (localThreadIndex != 0) {
		return;
	}

	const uint tileIndex = groupID.y * tileCountX + groupID.x;
	const uint2 tileCoord = groupID.xy;

	TileLightGridEntry grid;
	grid.offset = tileIndex * maxLocalLightsPerTile;
	grid.count = 0;
	grid.pointCount = 0;
	grid.spotCount = 0;

	if (maxLocalLightsPerTile == 0) {
		gTileLightGrid[tileIndex] = grid;
		return;
	}
	if (lightCullingEnabled == 0u) {
		gTileLightGrid[tileIndex] = grid;
		return;
	}

	// 未使用スロットを明示的に無効値で初期化。
	// PIX確認が終わったら、負荷削減のため削除してもよいです。
	for (uint i = 0; i < maxLocalLightsPerTile; ++i) {
		gTileLightIndexList[grid.offset + i] = 0xFFFFFFFFu;
	}

	//============================================================================
	// point lights
	//============================================================================
	for (uint pointIndex = 0; pointIndex < cullPointLightCount; ++pointIndex) {

		if (grid.count >= maxLocalLightsPerTile) {
			break;
		}

		PointLight light = gPointLights[pointIndex];

		uint2 minTile;
		uint2 maxTile;

		if (!ComputeSphereTileBounds(light.pos, light.radius, minTile, maxTile)) {
			continue;
		}

		if (!TileContainsLight(tileCoord, minTile, maxTile)) {
			continue;
		}

		gTileLightIndexList[grid.offset + grid.count] = pointIndex;

		grid.count++;
		grid.pointCount++;
	}

	//============================================================================
	// spot lights
	//============================================================================
	for (uint spotIndex = 0; spotIndex < cullSpotLightCount; ++spotIndex) {

		if (grid.count >= maxLocalLightsPerTile) {
			break;
		}

		SpotLight light = gSpotLights[spotIndex];

		uint2 minTile;
		uint2 maxTile;

		// spot は一旦 distance を半径とした球近似でタイル判定
		if (!ComputeSphereTileBounds(light.pos, light.distance, minTile, maxTile)) {
			continue;
		}

		if (!TileContainsLight(tileCoord, minTile, maxTile)) {
			continue;
		}

		// PS側では localLightIndex < pointCount なら point、
		// それ以外は localLightIndex - pointCount を spot index として扱う。
		gTileLightIndexList[grid.offset + grid.count] = cullPointLightCount + spotIndex;

		grid.count++;
		grid.spotCount++;
	}

	gTileLightGrid[tileIndex] = grid;

}
