//============================================================================
//	include
//============================================================================
#include "../Common/defaultMesh.hlsli"
#include "../Common/meshLighting.hlsli"
#include "../Common/meshPBRMaterial.hlsli"
#include "../Common/meshPBR.hlsli"
#include "../Common/meshSurfaceLighting.hlsli"
#include "../../Common/temporalMotion.hlsli"
#include "../../Atmosphere/transparentFog.hlsli"

//============================================================================
//	output
//============================================================================
struct TransparentPSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	mainTransparent
//============================================================================
TransparentPSOutput mainTransparent(VSOutput input) {

	ResolvedPBRMaterial m = ResolvePBRMaterial(input);
	// フォワードレンダリングでPBR処理
	float3 finalColor = EvaluateMeshSurfaceLighting(input, m);

	TransparentPSOutput output;
	output.color = ApplyTransparentFog(float4(finalColor, m.baseColor.a), input.position);
	return output;
}
