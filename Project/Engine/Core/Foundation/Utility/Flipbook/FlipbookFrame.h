#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <cstdint>
#include <span>

namespace Engine {

	//============================================================================
	//	FlipbookFrame
	//	フリップブックのコマUV
	//============================================================================
	struct FlipbookFrame {

		// コマ1つ分のUVスケール
		Vector2 uvScale = Vector2::AnyInit(1.0f);
		// コマの位置のUVオフセット
		Vector2 uvOffset = Vector2::AnyInit(0.0f);
	};

	// 分割数と進行度0~1からコマUVを求める、左上から右下の順に送る
	FlipbookFrame CalcFlipbookFrame(std::span<const int32_t> tilesX, int32_t tilesY, float progress);
} // Engine
