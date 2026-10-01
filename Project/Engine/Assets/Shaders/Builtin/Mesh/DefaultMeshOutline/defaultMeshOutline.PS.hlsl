//============================================================================
//	include
//============================================================================
#include "../Common/defaultMeshOutline.hlsli"

//============================================================================
//	main
//============================================================================
float4 main(OutlineVertexOutput input) : SV_TARGET0 {

	ApplyMeshLODDither(input.position.xy, input.lodCoverage);
	return input.color;
}
