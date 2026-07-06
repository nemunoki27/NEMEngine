//============================================================================
//	CBuffer
//============================================================================

cbuffer PostProcessFrameConstants : register(b0) {

	// 解像度情報
	float2 resolution;
	float2 invResolution;
	
	// 起動してからの経過時間
	float time;
	float deltaTime;
	// 経過フレーム
	uint frameIndex;
	float _pad0;

	// カメラ情報
	// 近/遠クリップ距離
	float cameraNear;
	float cameraFar;
	float _pad1;
	float _pad2;

	// ワールド座標
	float3 cameraWorldPos;
	float _pad3;
	
	// ビュー情報
	float4x4 cameraView;
	float4x4 cameraViewInverse;
	float4x4 cameraProjection;
	float4x4 cameraProjectionInverse;
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

// 標準DXのLH射影[0,1]の非線形深度を、near..farを0..1へ正規化した線形深度へ変換する
float LinearizeDepth01(float rawDepth) {

	float eyeZ = (cameraFar * cameraNear) / max(cameraFar - rawDepth * (cameraFar - cameraNear), 1e-6f);
	return saturate((eyeZ - cameraNear) / max(cameraFar - cameraNear, 1e-6f));
}