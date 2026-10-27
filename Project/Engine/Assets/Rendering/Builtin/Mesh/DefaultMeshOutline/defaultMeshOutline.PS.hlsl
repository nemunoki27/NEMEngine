//============================================================================
//	include
//============================================================================
#include "../Common/defaultMeshOutline.hlsli"

//============================================================================
//	main
//============================================================================
float4 main(OutlineVertexOutput input) : SV_TARGET0 {

	ApplyMeshLODDither(input.position.xy, input.lodCoverage);
	if ((input.flags & MESH_OUTLINE_FLAG_RESPECT_MATERIAL_SURFACE) != 0u) {
		float alpha = input.baseAlpha;
		if (input.baseColorTextureIndex != 0xFFFFFFFFu) {
			Texture2D<float4> baseColor = NEM_TEXTURE2D(
				input.baseColorTextureIndex);
			alpha *= baseColor.Sample(gOutlineSampler, input.uv).a;
		}
		if (input.opacityTextureIndex != 0xFFFFFFFFu) {
			Texture2D<float4> opacity = NEM_TEXTURE2D(
				input.opacityTextureIndex);
			alpha *= opacity.Sample(gOutlineSampler, input.uv).r;
		}
		clip(alpha - input.alphaThreshold);
	}
	return input.color;
}
