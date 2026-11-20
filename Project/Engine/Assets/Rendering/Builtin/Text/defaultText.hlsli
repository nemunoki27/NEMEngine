#include "../Common/shineHighlight.hlsli"

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
	float2 layoutTexcoord : TEXCOORD2;
	uint instanceID : INSTANCEID;
};

struct VSInstance {

	float2 rectMin;
	float2 rectMax;
	float2 uvMin;
	float2 uvMax;
	float2 materialUVMin;
	float2 materialUVMax;
	float4x4 worldMatrix;
	float2 layoutUVMin;
	float2 layoutUVMax;
};
StructuredBuffer<VSInstance> gVSInstances : register(t0);

struct PSInstance {

	float2 atlasSize;
	float pxRange;
	float padding0;
	float4x4 uvMatrix;
	ShineHighlightData shine;
};
StructuredBuffer<PSInstance> gPSInstances : register(t2);
