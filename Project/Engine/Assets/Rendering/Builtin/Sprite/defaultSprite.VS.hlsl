//============================================================================
//	include
//============================================================================
#include "defaultSprite.hlsli"

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
struct VSInstance {

	float2 size;
	float2 pivot;

	float4x4 worldMatrix;
	float4x4 previousWorldMatrix;
};
StructuredBuffer<VSInstance> gVSInstances : register(t0);

//============================================================================
//	main
//============================================================================
VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {

	VSInstance instance = gVSInstances[instanceID];

	float4x4 wvp = mul(instance.worldMatrix, viewProjection);

	VSOutput output;

	// ピボットとサイズを考慮してローカル座標を計算
	float2 localPos = (input.position - instance.pivot) * instance.size;
	output.position = mul(float4(localPos, 0.0f, 1.0f), wvp);
	output.currentClipPosition = mul(mul(float4(localPos, 0.0f, 1.0f), instance.worldMatrix), unjitteredViewProjection);
	output.previousClipPosition = mul(mul(float4(localPos, 0.0f, 1.0f), instance.previousWorldMatrix), previousViewProjection);
	output.texcoord = input.texcoord;
	output.instanceID = instanceID;

	return output;
}
