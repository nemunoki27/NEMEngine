#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ScreenSpaceOutline 共通定数
	//============================================================================
	// Dilationの半径上限(px)。CPU/HLSLで同じ値を使い、巨大半径によるGPU Hangを防ぐ
	// HLSL側 screenSpaceOutlineCommon.hlsli の kMaxScreenSpaceOutlineRadiusPixels と一致させること
	inline constexpr uint32_t kMaxScreenSpaceOutlineRadiusPixels = 16u;
} // Engine
