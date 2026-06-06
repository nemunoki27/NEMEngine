#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ScreenSpaceOutline GPU layout types
	//	HLSL(screenSpaceOutlineCommon.hlsli / screenSpaceOutlineMask.hlsli)と
	//	field順・paddingを完全一致させること
	//============================================================================

	// Style IDから引く描画パラメータ。dilation/compositeが参照する
	struct ScreenSpaceOutlineStyleGPU {

		Color4 color{};
		float widthPixels = 3.0f;
		int32_t priority = 0;
		uint32_t visibilityMode = 0;
		uint32_t regionMode = 0;
	};
	static_assert(sizeof(ScreenSpaceOutlineStyleGPU) % 16 == 0);

	// Mask描画1回ぶんのStyle IDとSubMesh制限。Mesh backendがMask pipelineへ渡す
	struct ScreenSpaceOutlineMaskConstants {

		uint32_t styleID = 0;
		int32_t restrictSubMeshIndex = -1;
		uint32_t _pad0 = 0;
		uint32_t _pad1 = 0;
	};
	static_assert(sizeof(ScreenSpaceOutlineMaskConstants) % 16 == 0);

	// Dilation Computeの定数
	struct ScreenSpaceOutlineDilateConstants {

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t styleCount = 0;
		uint32_t maxRadiusPixels = 0;
	};
	static_assert(sizeof(ScreenSpaceOutlineDilateConstants) % 16 == 0);

	// Composite Pixel Shaderの定数。styleID範囲外のBuffer読みを防ぐ
	struct ScreenSpaceOutlineCompositeConstants {

		uint32_t styleCount = 0;
		uint32_t _pad0 = 0;
		uint32_t _pad1 = 0;
		uint32_t _pad2 = 0;
	};
	static_assert(sizeof(ScreenSpaceOutlineCompositeConstants) % 16 == 0);

	// Style数の上限。超過分は無視してログを出す
	inline constexpr uint32_t kMaxScreenSpaceOutlineStyles = 256;
} // Engine
