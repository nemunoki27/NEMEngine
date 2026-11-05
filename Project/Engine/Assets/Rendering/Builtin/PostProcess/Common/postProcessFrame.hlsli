#ifndef NEM_POST_PROCESS_FRAME_HLSLI
#define NEM_POST_PROCESS_FRAME_HLSLI

cbuffer PostProcessFrameConstants : register(b0) {

	float2 resolution;
	float2 invResolution;
	float time;
	float deltaTime;
	uint frameIndex;
	float _framePad;
	float cameraNear;
	float cameraFar;
	float2 framePadding;
	float3 cameraWorldPos;
	float _positionPad;
	float4x4 cameraView;
	float4x4 cameraViewInverse;
	float4x4 cameraProjection;
	float4x4 cameraProjectionInverse;
};

#endif
