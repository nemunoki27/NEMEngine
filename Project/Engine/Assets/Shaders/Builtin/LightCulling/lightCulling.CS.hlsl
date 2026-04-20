//============================================================================
//	resources
//============================================================================

// ライト
cbuffer LightCounts : register(b0) {

	uint directionalCount;
	uint pointCount;
	uint spotCount;
	uint localCount;
};
cbuffer LightCullingParams : register(b1) {

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
	uint _pad0;

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
void main(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {

	if (groupID.x >= tileCountX || groupID.y >= tileCountY) {
		return;
	}

	const uint localThreadIndex = groupThreadID.y * 16u + groupThreadID.x;

	if (localThreadIndex == 0) {
		gTileMinDepthBits = 0x7F7FFFFF;
		gTileMaxDepthBits = 0;
		gTileValidDepthCount = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	const uint2 pixelCoord = groupID.xy * uint2(tileSizeX, tileSizeY) + groupThreadID.xy;
	if (pixelCoord.x < screenWidth && pixelCoord.y < screenHeight) {

		float depthValue = gSourceDepth.Load(int3(pixelCoord, 0));

		// 背景(=1.0)は無視して、実際に描かれたジオメトリだけを見る
		if (depthValue < 0.999999f) {
			InterlockedMin(gTileMinDepthBits, asuint(depthValue));
			InterlockedMax(gTileMaxDepthBits, asuint(depthValue));
			InterlockedAdd(gTileValidDepthCount, 1u);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (localThreadIndex != 0) {
		return;
	}

	const uint tileIndex = groupID.y * tileCountX + groupID.x;

	TileLightGridEntry grid;
	grid.offset = tileIndex * maxLocalLightsPerTile;
	grid.count = 0;
	grid.pointCount = 0;
	grid.spotCount = 0;

	// このタイルに実際のジオメトリがないなら、ライトリストは空でよい
	if (gTileValidDepthCount == 0) {
		gTileLightGrid[tileIndex] = grid;
		return;
	}

	float tileMinViewZ = ReconstructViewZ(asfloat(gTileMinDepthBits));
	float tileMaxViewZ = ReconstructViewZ(asfloat(gTileMaxDepthBits));

	if (tileMaxViewZ < tileMinViewZ) {
		float temp = tileMinViewZ;
		tileMinViewZ = tileMaxViewZ;
		tileMaxViewZ = temp;
	}

	const uint2 tileCoord = groupID.xy;

	//============================================================================
	//	point
	//============================================================================
	for (uint index0 = 0; index0 < pointCount; ++index0) {

		PointLight light = gPointLights[index0];

		uint2 minTile;
		uint2 maxTile;
		if (!ComputeSphereTileBounds(light.pos, light.radius, minTile, maxTile)) {
			continue;
		}
		if (!TileContainsLight(tileCoord, minTile, maxTile)) {
			continue;
		}

		float lightViewZ = mul(float4(light.pos, 1.0f), viewMatrix).z;
		if (!OverlapsDepthRange(lightViewZ, light.radius, tileMinViewZ, tileMaxViewZ)) {
			continue;
		}

		if (grid.count < maxLocalLightsPerTile) {
			gTileLightIndexList[grid.offset + grid.count] = index0;
			grid.count++;
			grid.pointCount++;
		}
	}

	//============================================================================
	//	spot
	//============================================================================
	for (uint index1 = 0; index1 < spotCount; ++index1) {

		SpotLight light = gSpotLights[index1];

		// まずは球近似で十分
		uint2 minTile;
		uint2 maxTile;
		if (!ComputeSphereTileBounds(light.pos, light.distance, minTile, maxTile)) {
			continue;
		}
		if (!TileContainsLight(tileCoord, minTile, maxTile)) {
			continue;
		}

		float lightViewZ = mul(float4(light.pos, 1.0f), viewMatrix).z;
		if (!OverlapsDepthRange(lightViewZ, light.distance, tileMinViewZ, tileMaxViewZ)) {
			continue;
		}

		if (grid.count < maxLocalLightsPerTile) {
			gTileLightIndexList[grid.offset + grid.count] = pointCount + index1;
			grid.count++;
			grid.spotCount++;
		}
	}

	gTileLightGrid[tileIndex] = grid;
}