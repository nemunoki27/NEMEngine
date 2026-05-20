//============================================================================
//	include
//============================================================================

#include "../PostProcessCommon.hlsli"

//============================================================================
//	CBuffer
//============================================================================

struct Material {
	
	// 1.0fが最大のグレースケール割合
	float rate;
};

//============================================================================
//	buffer
//============================================================================

ConstantBuffer<Material> gMaterial : register(b0);

//============================================================================
//	Main
//============================================================================
[numthreads(THREAD_POSTPROCESS_GROUP, THREAD_POSTPROCESS_GROUP, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
	
	uint width, height;
	gInputTexture.GetDimensions(width, height);

	// ピクセル位置
	uint2 pixelPos = DTid.xy;

	// 範囲外
	if (pixelPos.x >= width || pixelPos.y >= height) {
		return;
	}

	// フラグが立っていなければ処理しない
	if (!CheckPixelBitMask(Bit_Grayscale, pixelPos)) {
	
		gOutputTexture[pixelPos] = gInputTexture.Load(int3(pixelPos, 0));
		return;
	}

	// 入力カラー
	float4 color = gInputTexture.Load(int3(pixelPos, 0));
	// 完全グレースケール値
	float gray = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
	// 補間割合で元色とグレーをブレンド
	float3 finalColor = lerp(color.rgb, gray.xxx, saturate(gMaterial.rate));

	// 出力
	gOutputTexture[pixelPos] = float4(finalColor, color.a);
}