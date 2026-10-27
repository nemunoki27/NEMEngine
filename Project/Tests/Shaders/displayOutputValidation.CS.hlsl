// 本番の色変換を計算Shaderから検証する
#define main DisplayOutputPixelMain
#include "../../Engine/Assets/Rendering/Builtin/ToneMapToView/outputTransform.PS.hlsl"
#undef main

RWTexture2D<float4> gResult : register(u0);

[numthreads(8, 1, 1)]
void main(uint3 index : SV_DispatchThreadID) {

	if (index.x >= 4) return;
	const float4 color = gTexture.Load(int3(index.xy, 0));
	gResult[index.xy] = float4(TransformDisplayOutput(color.rgb, outputMode, paperWhiteNits), color.a);
}
