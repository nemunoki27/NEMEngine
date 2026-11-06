//============================================================================
//	Common VS/PS
//============================================================================
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float2 referenceToRenderScale;
	float2 viewPadding;
};

struct VSOutput {

	float4 position : SV_Position;
	float2 texcoord : TEXCOORD0;
	float2 materialTexcoord : TEXCOORD1;
	uint instanceID : INSTANCEID;
};
