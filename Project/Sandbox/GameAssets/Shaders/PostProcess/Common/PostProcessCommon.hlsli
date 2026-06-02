//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessFrameConstants : register(b0) {

	float2 resolution;
	float2 invResolution;
	float time;
	float deltaTime;
	uint frameIndex;
	float framePadding;
};

//============================================================================
//	Texture
//============================================================================

Texture2D<float4> gSourceColor : register(t0);
RWTexture2D<float4> gDestColor : register(u0);

//============================================================================
//	Function
//============================================================================

bool IsOutside(uint2 pixelPos) {

	return pixelPos.x >= (uint)resolution.x || pixelPos.y >= (uint)resolution.y;
}

int2 ClampPixel(int2 pixelPos) {

	return clamp(pixelPos, int2(0, 0), int2((uint)resolution.x - 1u, (uint)resolution.y - 1u));
}

float4 LoadSource(uint2 pixelPos) {

	return gSourceColor.Load(int3(pixelPos, 0));
}

float4 LoadSourceClamp(int2 pixelPos) {

	return gSourceColor.Load(int3(ClampPixel(pixelPos), 0));
}
