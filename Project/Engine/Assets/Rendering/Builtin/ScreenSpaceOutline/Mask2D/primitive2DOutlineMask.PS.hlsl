//============================================================================
//	include
//============================================================================
#include "../../Primitive/primitive2D.hlsli"
#include "../Common/screenSpaceOutlineCommon.hlsli"

//============================================================================
//	resources
//============================================================================
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState gSampler : register(s0);

cbuffer MaterialParameters : register(b3) {

	float4 color;
};

cbuffer ScreenSpaceOutlineMaskConstantsBuffer : register(b1, space1) {

	ScreenSpaceOutlineMaskConstants gMaskConstants;
};

//============================================================================
//	main
//============================================================================
uint main(VSOutput input) : SV_Target0 {

	if (gMaskConstants.styleID == 0u) {
		discard;
	}

	const float2 uv = ResolvePrimitivePixelUV(input.texcoord, input.uvCoordinates, input.ringParams, input.uvBasis);
	const HexTilingData hex = BuildHexTiling(uv, input.hexTiling);
	const float4 textureColor = SamplePrimitiveMaterialTexture(baseColorTexture, gSampler, hex, unassignedMaterialTextureMask, 0u);
	const float4 outputColor = textureColor * color;
	const float alpha = ResolveScreenSpaceOutlineAlpha(
		textureColor, outputColor, gMaskConstants.alphaSource);
	clip(alpha - gMaskConstants.alphaThreshold);
	return gMaskConstants.styleID;
}
