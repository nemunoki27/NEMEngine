//============================================================================
//	include
//============================================================================

#include "../PostProcessCommon.hlsli"

//============================================================================
//	Constant
//============================================================================

// 3x3のオフセット
static const int2 kIndex3x3[3][3] = {
	{ { -1, -1 }, { 0, -1 }, { 1, -1 } },
	{ { -1, 0 }, { 0, 0 }, { 1, 0 } },
	{ { -1, 1 }, { 0, 1 }, { 1, 1 } }
};

// Prewitt フィルタ (横方向)
static const float kPrewittHorizontalKernel[3][3] = {
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f }
};

// Prewitt フィルタ (縦方向)
static const float kPrewittVerticalKernel[3][3] = {
	{ -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f }
};

//============================================================================
//	CBuffer
//============================================================================

struct OutlineMaterial {
	
	float4x4 projectionInverse;
	
	float edgeScale;
	float threshold;
	
	float3 color;
};
ConstantBuffer<OutlineMaterial> gMaterial : register(b0);

//============================================================================
//	Texture
//============================================================================

Texture2D<float> gDepthTexture : register(t2);
SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

//============================================================================
//	main
//============================================================================
[numthreads(THREAD_POSTPROCESS_GROUP, THREAD_POSTPROCESS_GROUP, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {

	uint width, height;
	gInputTexture.GetDimensions(width, height);
	
	 // ピクセル位置
	uint2 pixelPos = DTid.xy;
	
	// 画像範囲外チェック
	if (pixelPos.x >= width || pixelPos.y >= height) {
		return;
	}

	// 近傍(3x3)のどこかに Bit_DepthBasedOutline が立っていたら処理する
	// 自分のピクセルが立ってなくても、外側アウトライン用に通す
	bool anyMask = false;
	{
		int2 maxP = int2(int(width) - 1, int(height) - 1);
		[unroll]
		for (int y = 0; y < 3; ++y) {
			[unroll]
			for (int x = 0; x < 3; ++x) {

				int2 p = clamp(int2(pixelPos) + kIndex3x3[x][y], int2(0, 0), maxP);
				if (CheckPixelBitMask(Bit_DepthBasedOutline, uint2(p))) {
					
					anyMask = true;
				}
			}
		}
	}
	// フラグが立っていなければ処理しない
	if (!anyMask) {
		gOutputTexture[pixelPos] = gInputTexture.Load(int3(pixelPos, 0));
		return;
	}

	float2 gradient = float2(0.0f, 0.0f);

	// サンプリング処理
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {

			int2 offset = kIndex3x3[x][y];
			int2 sampleP = clamp(int2(pixelPos) + offset, int2(0, 0), int2(int(width) - 1, int(height) - 1));

			float2 uv = (float2(sampleP) + 0.5f) / float2(width, height);
			float ndcDepth = gDepthTexture.SampleLevel(gSamplerPoint, uv, 0).r;

			// NDC -> clip
			float2 clipXY = uv * 2.0f - 1.0f;
			clipXY.y *= -1.0f;
			float4 clipPos = float4(clipXY, ndcDepth, 1.0f);

			// clip -> view
			float4 viewPos = mul(clipPos, gMaterial.projectionInverse);
			viewPos /= viewPos.w;
			float viewZ = viewPos.z;

			gradient.x += viewZ * kPrewittHorizontalKernel[x][y];
			gradient.y += viewZ * kPrewittVerticalKernel[x][y];
		}
	}
	// エッジの重み計算
	float weight = saturate(max(length(gradient) - gMaterial.threshold, 0.0f) * gMaterial.edgeScale);

	// 元カラーと合成して出力
	float2 uv = (float2(pixelPos) + 0.5f) / float2(width, height);
	float4 srcColor = gInputTexture.SampleLevel(gSamplerLinear, uv, 0);
	float3 finalColor = lerp(gMaterial.color, srcColor.rgb, 1.0f - weight);
	
	// 出力
	gOutputTexture[pixelPos] = float4(finalColor, 1.0f);
}