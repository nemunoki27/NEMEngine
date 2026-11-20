//============================================================================
//	include
//============================================================================
#include "../Common/defaultMeshOutline.hlsli"
#include "../../Common/hexTiling.hlsli"

//============================================================================
//	main
//============================================================================
float4 main(OutlineVertexOutput input) : SV_TARGET0 {

	if ((input.flags & MESH_OUTLINE_FLAG_RESPECT_MATERIAL_SURFACE) != 0u) {
		const HexTilingData hex = BuildHexTiling(input.uv, input.hexTiling);
		const float2 dx = ddx(input.uv), dy = ddy(input.uv);
		float alpha = input.baseAlpha;
		if (input.baseColorTextureIndex != 0xFFFFFFFFu) {
			Texture2D<float4> baseColor = NEM_TEXTURE2D(
				input.baseColorTextureIndex);
			alpha *= SampleHexGrad(baseColor, gOutlineSampler, hex, dx, dy, false).a;
		}
		if (input.opacityTextureIndex != 0xFFFFFFFFu) {
			Texture2D<float4> opacity = NEM_TEXTURE2D(
				input.opacityTextureIndex);
			alpha *= SampleHexGrad(opacity, gOutlineSampler, hex, dx, dy, false).r;
		}
		clip(alpha - input.alphaThreshold);
	}
	return input.color;
}
