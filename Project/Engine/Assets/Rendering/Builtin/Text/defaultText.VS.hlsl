//============================================================================
//	include
//============================================================================
#include "defaultText.hlsli"

//============================================================================
//	input
//============================================================================
struct VSInput {

	float2 position : POSITION;
	float2 texcoord : TEXCOORD0;
};

//============================================================================
//	resources
//============================================================================
//============================================================================
//	main
//============================================================================
VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {

	VSInstance instance = gVSInstances[instanceID];

	float4x4 wvp = mul(instance.worldMatrix, viewProjection);

	VSOutput output;
	float2 localPos = lerp(instance.rectMin, instance.rectMax, input.position);
	output.position = mul(float4(localPos, 0.0f, 1.0f), wvp);
	output.texcoord = lerp(instance.uvMin, instance.uvMax, input.texcoord);
	output.materialTexcoord = lerp(instance.materialUVMin, instance.materialUVMax, input.texcoord);
	output.instanceID = instanceID;

	return output;
}
