#ifndef NEM_MESH_LIGHTING_HLSLI
#define NEM_MESH_LIGHTING_HLSLI

//============================================================================
//	include
//============================================================================
#include "lightingCommon.hlsli"
#include "../../Common/hexTiling.hlsli"
#include "../../Common/descriptorHeapCompatibility.hlsli"

//============================================================================
//	Mesh描画のライティング共通定義、VSOutput依存部分
//============================================================================

//============================================================================
//	ワールド法線の計算
//============================================================================
float3 ComputeWorldNormal(VSOutput input, uint normalTextureIndex, HexTilingData hex, float2 dx, float2 dy) {

	float3 N = normalize(input.normal);
	if (normalTextureIndex == kNoTexture) {
		return N;
	}

	// TBN構築
	float3x3 TBN = BuildMeshTBN(input);
	Texture2D<float4> normalTex = NEM_TEXTURE2D(normalTextureIndex);
	float3 tangentNormal = SampleHexGrad(normalTex, gSampler, hex, dx, dy, true).xyz * 2.0f - 1.0f;

	return normalize(mul(tangentNormal, TBN));
}

#endif // NEM_MESH_LIGHTING_HLSLI
