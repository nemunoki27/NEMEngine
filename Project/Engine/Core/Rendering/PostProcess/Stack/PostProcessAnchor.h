#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	PostProcessAnchor enum
	//	ポストプロセスを差し込む固定パス上の位置、選択肢の増減はこのenumの編集だけで済む
	//============================================================================
	enum class PostProcessAnchor : uint8_t {

		AfterLighting,             // ライティング直後で不透明のみの結果
		AfterRaytracingReflection, // レイトレ反射合成後
		AfterTransparent,          // 半透明合成後
		AfterMaskedUI,             // MaskedUI合成後で従来の既定位置
		BeforeBlit,                // ビューへのBlit直前で表示直前の最終結果
	};
} // Engine
