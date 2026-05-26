#include "../Common/PostProcessCommon.hlsli"

#define BLOCK_W 8
#define BLOCK_H 8
#define RADIUS_MAX 10

//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessParameters : register(b1) {

	float threshold;
	int radius;
	float sigma;
	float intensity;
};

//============================================================================
//	Function
//============================================================================

static const float3 LUMA = float3(0.2125f, 0.7154f, 0.0721f);

float Gaussian1D(int d, float s) {
	float x = (float) d;
	return exp(-(x * x) / (2.0f * s * s));
}

// 共有メモリ
groupshared float3 sTile[(BLOCK_H + 2 * RADIUS_MAX) * (BLOCK_W + 2 * RADIUS_MAX)];
// 横ブラー結果
groupshared float3 sHorz[(BLOCK_H + 2 * RADIUS_MAX) * BLOCK_W];

inline uint TilePitch() {
	
	return (BLOCK_W + 2 * RADIUS_MAX);
}
inline uint TileIndex(uint lx, uint ly) {
	
	return ly * TilePitch() + lx;
}
inline uint HorzIndex(uint lx, uint ly) {
	
	return ly * BLOCK_W + lx;
}

//============================================================================
//	Main
//============================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID) {

	uint2 pixelPos = DTid.xy;

	uint width, height;
	gSourceColor.GetDimensions(width, height);

	const int2 groupBase = int2(Gid.xy) * int2(BLOCK_W, BLOCK_H);
	const int2 p = int2(groupBase + int2(GTid.xy));
	const bool inBounds = (p.x >= 0 && p.y >= 0 && p.x < int(width) && p.y < int(height));

	int r = clamp(radius, 0, RADIUS_MAX);
	
	//============================================================================
	// プレフィルタをタイル＋ハローで共有メモリにロード
	//============================================================================
	
	const uint tileW = BLOCK_W + 2 * r;
	const uint tileH = BLOCK_H + 2 * r;

	// タイル内を分担ロード
	for (uint ly = GTid.y; ly < tileH; ly += BLOCK_H) {

		// グローバルY
		int gy = int(groupBase.y) + int(ly) - r;
		gy = clamp(gy, 0, int(height) - 1);

		for (uint lx = GTid.x; lx < tileW; lx += BLOCK_W) {

			// グローバルX
			int gx = int(groupBase.x) + int(lx) - r;
			gx = clamp(gx, 0, int(width) - 1);

			int2 sp = int2(gx, gy);
			float3 rgb = LoadSource(sp).rgb;

			float3 outRGB = 0.0f;
			float lum = dot(rgb, LUMA);
			if (lum >= threshold) {
					
				outRGB = rgb;
			}
			sTile[TileIndex(lx, ly)] = outRGB;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	//============================================================================
	// 横ブラー
	//============================================================================
	
	for (uint ly = GTid.y; ly < tileH; ly += BLOCK_H) {

		if (GTid.x >= BLOCK_W) {
			continue;
		}

		float3 accum = 0.0f;
		float wsum = 0.0f;
		const uint lxCenter = r + GTid.x;

		[loop]
		for (int dx = -r; dx <= r; ++dx) {

			 // タイル内X
			uint lx = lxCenter + dx;
			float w = Gaussian1D(dx, sigma);
			float3 c = sTile[TileIndex(lx, ly)];
			accum += c * w;
			wsum += w;
		}

		float3 h = (wsum > 0.0f) ? (accum / wsum) : 0.0f;
		sHorz[HorzIndex(GTid.x, ly)] = h;
	}

	GroupMemoryBarrierWithGroupSync();

	//============================================================================
	// 縦ブラー、合成
	//============================================================================

	if (inBounds) {

		const uint lyCenter = r + GTid.y;
		float3 accum = 0.0f;
		float wsum = 0.0f;

		[loop]
		for (int dy = -r; dy <= r; ++dy) {

			uint ly = lyCenter + dy;
			float3 c = sHorz[HorzIndex(GTid.x, ly)];
			float w = Gaussian1D(dy, sigma);
			accum += c * w;
			wsum += w;
		}

		float3 bloom = (wsum > 0.0f) ? (accum / wsum) : 0.0f;
		float3 scene = LoadSource(p).rgb;
		gDestColor[uint2(p)] = float4(scene + bloom * intensity, 1.0f);
	}
}