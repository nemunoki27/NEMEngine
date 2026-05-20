//============================================================================
//	include
//============================================================================

#include "../PostProcessCommon.hlsli"

//============================================================================
//	Constants
//============================================================================

#define BLOCK_W THREAD_POSTPROCESS_GROUP
#define BLOCK_H THREAD_POSTPROCESS_GROUP
#define RADIUS_MAX 10

//============================================================================
//	CBuffer
//============================================================================

cbuffer Parameter : register(b0) {
	
	float threshold;
	int radius;
	float sigma;
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
[numthreads(BLOCK_W, BLOCK_H, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID) {

	uint width, height;
	gInputTexture.GetDimensions(width, height);

	// このグループのコア領域の左上
	const int2 groupBase = int2(Gid.xy) * int2(BLOCK_W, BLOCK_H);

	// 画像外なら早期リターン
	if (groupBase.x + GTid.x >= width && groupBase.y + GTid.y >= height) {
		return;
	}

	// 実際にこのスレッドが担当する出力ピクセル
	const int2 p = int2(groupBase + int2(GTid.xy));
	const bool inBounds = (p.x < width) && (p.y < height);

	// 半径クランプ
	int r = min(radius, RADIUS_MAX);

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
			float3 rgb = gInputTexture.Load(int3(sp, 0)).rgb;

			float3 outRGB = 0.0f;
			if (CheckPixelBitMask(Bit_Bloom, sp)) {
				
				float lum = dot(rgb, LUMA);
				if (lum >= threshold) {
					
					outRGB = rgb;
				}
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
		float3 scene = gInputTexture.Load(int3(p, 0)).rgb;
		if (!CheckPixelBitMask(Bit_Bloom, p)) {

			gOutputTexture[p] = float4(scene, 1.0f);
		} else {
			
			gOutputTexture[p] = float4(scene + bloom, 1.0f);
		}
	}
}