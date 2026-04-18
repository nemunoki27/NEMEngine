//============================================================================
//	include
//============================================================================

#include "FullscreenCopy.hlsli"

//============================================================================
//	constants
//============================================================================

static const uint kNumVertex = 3;
static const float4 kPositions[kNumVertex] = {
	{ -1.0f, 1.0f, 0.0f, 1.0f },
	{ 3.0f, 1.0f, 0.0f, 1.0f },
	{ -1.0f, -3.0f, 0.0f, 1.0f }
};
static const float2 kTexcoords[kNumVertex] = {
	{ 0.0f, 0.0f },
	{ 2.0f, 0.0f },
	{ 0.0f, 2.0f }
};

//============================================================================
//	main
//============================================================================
VSOutput main(uint vertexID : SV_VertexID) {

	VSOutput output;
	
	output.position = kPositions[vertexID];
	output.texcoord = kTexcoords[vertexID];

	return output;
}