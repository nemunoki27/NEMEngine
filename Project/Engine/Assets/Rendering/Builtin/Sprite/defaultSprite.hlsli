//============================================================================
//	Common VS/PS
//============================================================================
#include "../Common/temporalMotion.hlsli"
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float4x4 unjitteredViewProjection;
	float4x4 previousViewProjection;
	float materialPadding;
	float3 _viewPad;
};

struct VSOutput {

	float4 position : SV_Position;
	float2 texcoord : TEXCOORD0;
	uint instanceID : INSTANCEID;
	float4 currentClipPosition : TEXCOORD6;
	float4 previousClipPosition : TEXCOORD7;
};
