//============================================================================
//	resources
//============================================================================
cbuffer LightCullingParams : register(b0) {

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

//============================================================================
//	constants
//============================================================================
static const uint kLightCullingModeDisabled = 0u;
static const uint kLightCullingModeTile2D = 1u;
static const uint kLightCullingModeClustered = 2u;
static const uint kLightCullingModeDebugAllLightsPerCluster = 3u;

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

float2 PixelToNDC(float2 pixel) {

	float2 safeScreenSize = max(float2(screenWidth, screenHeight), float2(1.0f, 1.0f));
	float2 uv = pixel / safeScreenSize;
	return float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
}

float3 ViewPositionFromNDCAndViewZ(float2 ndc, float viewZ) {

	float projX = max(abs(projectionMatrix[0][0]), 1e-5f);
	float projY = max(abs(projectionMatrix[1][1]), 1e-5f);
	return float3(ndc.x * viewZ / projX, ndc.y * viewZ / projY, viewZ);
}

void ExpandAABB(inout float3 aabbMin, inout float3 aabbMax, float3 p) {

	aabbMin = min(aabbMin, p);
	aabbMax = max(aabbMax, p);
}

void ComputeClusterZRange(uint clusterZ, out float zMin, out float zMax) {

	uint safeClusterCountZ = max(clusterCountZ, 1u);
	float safeNear = max(nearClip, 1e-4f);
	float safeFar = max(farClip, safeNear + 1e-3f);

	float z0 = (float) clusterZ / (float) safeClusterCountZ;
	float z1 = (float) (clusterZ + 1u) / (float) safeClusterCountZ;
	zMin = lerp(safeNear, safeFar, z0);
	zMax = lerp(safeNear, safeFar, z1);
}

void ComputeClusterAABB(uint2 tileCoord, uint clusterZ, out float3 aabbMin, out float3 aabbMax) {

	float2 pixelMin = float2(tileCoord * uint2(tileSizeX, tileSizeY));
	float2 pixelMax = float2(min((tileCoord + 1u) * uint2(tileSizeX, tileSizeY), uint2(screenWidth, screenHeight)));
	float2 ndcMin = PixelToNDC(pixelMin);
	float2 ndcMax = PixelToNDC(pixelMax);

	float zMin;
	float zMax;
	ComputeClusterZRange(clusterZ, zMin, zMax);

	aabbMin = float3(1e30f, 1e30f, 1e30f);
	aabbMax = float3(-1e30f, -1e30f, -1e30f);

	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMin.x, ndcMin.y), zMin));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMax.x, ndcMin.y), zMin));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMin.x, ndcMax.y), zMin));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMax.x, ndcMax.y), zMin));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMin.x, ndcMin.y), zMax));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMax.x, ndcMin.y), zMax));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMin.x, ndcMax.y), zMax));
	ExpandAABB(aabbMin, aabbMax, ViewPositionFromNDCAndViewZ(float2(ndcMax.x, ndcMax.y), zMax));
}

bool SphereIntersectsAABB(float3 center, float radius, float3 aabbMin, float3 aabbMax) {

	float3 closest = clamp(center, aabbMin, aabbMax);
	float3 delta = center - closest;
	float safeRadius = max(radius, 0.0f);
	return dot(delta, delta) <= safeRadius * safeRadius;
}

bool AppendLightIndex(inout TileLightGridEntry grid, uint lightIndex, bool isPointLight, uint listCapacity) {

	if (grid.count >= listCapacity) {
		return false;
	}
	gTileLightIndexList[grid.offset + grid.count] = lightIndex;
	grid.count++;
	if (isPointLight) {
		grid.pointCount++;
	} else {
		grid.spotCount++;
	}
	return true;
}

//============================================================================
//	main
//============================================================================
[numthreads(16, 16, 1)]
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {

	uint safeTileCountX = max(tileCountX, 1u);
	uint safeTileCountY = max(tileCountY, 1u);
	uint safeClusterCountZ = max(clusterCountZ, 1u);
	const bool usesClusterGrid =
		lightCullingMode == kLightCullingModeClustered ||
		lightCullingMode == kLightCullingModeDebugAllLightsPerCluster;

	if (groupID.x >= safeTileCountX || groupID.y >= safeTileCountY ||
		(usesClusterGrid && groupID.z >= safeClusterCountZ)) {
		return;
	}

	const uint localThreadIndex = groupThreadID.y * 16u + groupThreadID.x;

	// 1タイルにつき1スレッドだけがライトリストを書く
	if (localThreadIndex != 0) {
		return;
	}

	const uint tileIndex = groupID.y * safeTileCountX + groupID.x;
	const uint clusterIndex = usesClusterGrid ?
		(groupID.z * safeTileCountY + groupID.y) * safeTileCountX + groupID.x :
		tileIndex;
	const uint2 tileCoord = groupID.xy;
	const uint listCapacity = usesClusterGrid ? maxLocalLightsPerCluster : maxLocalLightsPerTile;

	TileLightGridEntry grid;
	grid.offset = clusterIndex * listCapacity;
	grid.count = 0;
	grid.pointCount = 0;
	grid.spotCount = 0;

	if (listCapacity == 0 || lightCullingEnabled == 0u || lightCullingMode == kLightCullingModeDisabled) {
		gTileLightGrid[clusterIndex] = grid;
		return;
	}

#if defined(NEM_LIGHT_CULLING_DEBUG_FILL_UNUSED)
	for (uint i = 0; i < listCapacity; ++i) {
		gTileLightIndexList[grid.offset + i] = 0xFFFFFFFFu;
	}
#endif

	if (lightCullingMode == kLightCullingModeDebugAllLightsPerCluster) {

		for (uint pointIndex = 0; pointIndex < cullPointLightCount; ++pointIndex) {
			if (!AppendLightIndex(grid, pointIndex, true, listCapacity)) {
				break;
			}
		}
		for (uint spotIndex = 0; spotIndex < cullSpotLightCount; ++spotIndex) {
			if (!AppendLightIndex(grid, cullPointLightCount + spotIndex, false, listCapacity)) {
				break;
			}
		}
		gTileLightGrid[clusterIndex] = grid;
		return;
	}

	float3 clusterAABBMin = 0.0f.xxx;
	float3 clusterAABBMax = 0.0f.xxx;
	if (lightCullingMode == kLightCullingModeClustered) {
		ComputeClusterAABB(tileCoord, groupID.z, clusterAABBMin, clusterAABBMax);
	}

	//============================================================================
	// point lights
	//============================================================================
	for (uint pointIndex = 0; pointIndex < cullPointLightCount; ++pointIndex) {

		if (grid.count >= listCapacity) {
			break;
		}

		PointLight light = gPointLights[pointIndex];

		if (lightCullingMode == kLightCullingModeClustered) {

			float3 lightViewPos = mul(float4(light.pos, 1.0f), viewMatrix).xyz;
			if (!SphereIntersectsAABB(lightViewPos, light.radius, clusterAABBMin, clusterAABBMax)) {
				continue;
			}
		} else {

			uint2 minTile;
			uint2 maxTile;

			if (!ComputeSphereTileBounds(light.pos, light.radius, minTile, maxTile)) {
				continue;
			}
			if (!TileContainsLight(tileCoord, minTile, maxTile)) {
				continue;
			}
		}

		AppendLightIndex(grid, pointIndex, true, listCapacity);
	}

	//============================================================================
	// spot lights
	//============================================================================
	for (uint spotIndex = 0; spotIndex < cullSpotLightCount; ++spotIndex) {

		if (grid.count >= listCapacity) {
			break;
		}

		SpotLight light = gSpotLights[spotIndex];

		if (lightCullingMode == kLightCullingModeClustered) {

			float3 lightViewPos = mul(float4(light.pos, 1.0f), viewMatrix).xyz;
			if (!SphereIntersectsAABB(lightViewPos, light.distance, clusterAABBMin, clusterAABBMax)) {
				continue;
			}
		} else {

			uint2 minTile;
			uint2 maxTile;

			// spot は初期実装では distance を半径とした球近似で判定する
			if (!ComputeSphereTileBounds(light.pos, light.distance, minTile, maxTile)) {
				continue;
			}
			if (!TileContainsLight(tileCoord, minTile, maxTile)) {
				continue;
			}
		}

		// PS側では localLightIndex < pointCount なら point、
		// それ以外は localLightIndex - pointCount を spot index として扱う
		AppendLightIndex(grid, cullPointLightCount + spotIndex, false, listCapacity);
	}

	gTileLightGrid[clusterIndex] = grid;

}
