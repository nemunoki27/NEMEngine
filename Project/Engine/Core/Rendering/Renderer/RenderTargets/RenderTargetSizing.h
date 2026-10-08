#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <optional>

namespace Engine {

	//============================================================================
	//	RenderTargetSize struct
	//	検証済みの描画先サイズ
	//============================================================================
	struct RenderTargetSize {

		uint32_t width = 1;
		uint32_t height = 1;
	};

	namespace RenderTargetSizing {

		// 倍率を適用しGPUで作成できるサイズを返す
		std::optional<RenderTargetSize> ResolveSize(uint32_t width, uint32_t height,
			float widthScale = 1.0f, float heightScale = 1.0f);
	}
}
