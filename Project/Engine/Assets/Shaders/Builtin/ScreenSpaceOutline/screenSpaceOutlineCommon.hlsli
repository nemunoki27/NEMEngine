#ifndef NEM_SCREEN_SPACE_OUTLINE_COMMON_HLSLI
#define NEM_SCREEN_SPACE_OUTLINE_COMMON_HLSLI

//============================================================================
//	Screen-space Outline 共通定義
//	CPU側 ScreenSpaceOutlineStyleGPU / 各Constants と完全一致させること
//============================================================================

struct ScreenSpaceOutlineStyleGPU {

	float4 color;
	float widthPixels;
	int priority;
	uint visibilityMode;
	uint regionMode;
};

struct ScreenSpaceOutlineMaskConstants {

	uint styleID;
	int restrictSubMeshIndex;
	uint2 padding;
};

struct ScreenSpaceOutlineDilateConstants {

	uint width;
	uint height;
	uint styleCount;
	uint maxRadiusPixels;
};

// Dilation半径の上限(px)。CPU側 kMaxScreenSpaceOutlineRadiusPixels と一致させること。
// 巨大半径によるGPU Hangを防ぐため、各Dilation Shaderはこれでclampしてからループする
static const uint kMaxScreenSpaceOutlineRadiusPixels = 16u;

#endif // NEM_SCREEN_SPACE_OUTLINE_COMMON_HLSLI
