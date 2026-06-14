//============================================================================
//	include
//============================================================================
#include "../Common/defaultMesh.hlsli"
#include "../Common/meshLighting.hlsli"
#include "../Common/meshLambert.hlsli"

//============================================================================
//	output
//============================================================================
struct PSOutput {

	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
	float4 worldPos : SV_TARGET2;
};

struct TransparentPSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	main
//	色とUVとベースカラーテクスチャだけを使い、ハーフランバートで3種のライトを受ける
//	PBRパラメータのmetallicやroughnessは一切参照しない
//============================================================================
PSOutput main(VSOutput input) {

	SubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);
	MeshMaterialParameters params = GetInstanceMeshMaterialParameters(input.instanceID, input.subMeshIndex);

	float2 uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;
	float4 baseColor = ResolveLambertBaseColor(params, uv);
	float3 N = ComputeWorldNormal(input, params.normalTexture, uv);

	// 平行光源は影無し、そのあと点とスポット
	float3 lit = 0.0f.xxx;
	[loop]
	for (uint i = 0; i < directionalCount; ++i) {
		lit += EvaluateLambertDirectional(gDirectionalLights[i], N);
	}
	lit += AccumulateLocalLambertLighting(input.worldPos, N);

	PSOutput output;
	output.color = ComposeLambertColor(params, uv, baseColor, lit);
	output.normal = float4(N * 0.5f + 0.5f, 1.0f);
	output.worldPos = float4(input.worldPos, 1.0f);
	return output;
}

TransparentPSOutput mainTransparent(VSOutput input) {

	PSOutput lit = main(input);

	TransparentPSOutput output;
	output.color = lit.color;
	return output;
}
