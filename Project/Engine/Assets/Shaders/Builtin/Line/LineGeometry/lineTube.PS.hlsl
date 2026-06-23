//============================================================================
//	include
//============================================================================
#include "lineTube.hlsli"

//============================================================================
//	main
//	チューブ表面の法線で簡易ライティングし、立体的な丸い線に見せる
//============================================================================
float4 main(TubeGSOutput input) : SV_TARGET0 {

	float3 normal = normalize(input.worldNormal);
	// 固定方向ライトでランバート、環境光を足して裏側も真っ黒にしない
	float3 lightDir = normalize(float3(0.4f, 0.8f, 0.5f));
	float lambert = saturate(dot(normal, lightDir));
	float shade = 0.4f + 0.6f * lambert;

	float4 color = input.color;
	color.rgb *= shade;
	return color;
}
