#ifndef NEM_POST_PROCESS_COMMON_HLSLI
#define NEM_POST_PROCESS_COMMON_HLSLI

cbuffer PostProcessFrameConstants : register(b0) {

	float2 resolution;
	float2 invResolution;
	float time;
	float deltaTime;
	uint frameIndex;
	float _framePad;
	float cameraNear;
	float cameraFar;
	float2 _cameraPad;
	float3 cameraWorldPos;
	float _positionPad;
	float4x4 cameraView;
	float4x4 cameraViewInverse;
	float4x4 cameraProjection;
	float4x4 cameraProjectionInverse;
};
Texture2D<float4> gSourceColor : register(t0);
RWTexture2D<float4> gDestColor : register(u0);

bool IsOutside(uint2 pixel) {

	return any(pixel >= uint2(resolution));
}

// 解像度の異なる入力も同じ画像領域から読む
float4 SampleSource(float2 uv) {

	uint width, height;
	gSourceColor.GetDimensions(width, height);
	float2 position = saturate(uv) * float2(width, height) - 0.5f;
	int2 base = int2(floor(position));
	float2 weight = frac(position);
	int2 maximum = int2(width, height) - 1;
	float4 top = lerp(gSourceColor.Load(int3(clamp(base, 0, maximum), 0)),
		gSourceColor.Load(int3(clamp(base + int2(1, 0), 0, maximum), 0)), weight.x);
	float4 bottom = lerp(gSourceColor.Load(int3(clamp(base + int2(0, 1), 0, maximum), 0)),
		gSourceColor.Load(int3(clamp(base + 1, 0, maximum), 0)), weight.x);
	return lerp(top, bottom, weight.y);
}

float SourceLuminance(float3 color) {

	return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

#endif
