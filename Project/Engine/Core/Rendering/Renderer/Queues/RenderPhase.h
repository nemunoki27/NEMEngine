#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <cstdint>
#include <string_view>

namespace Engine {

	//============================================================================
	//	RenderPhase enum
	//============================================================================

	enum class RenderPhase : uint8_t {

		Opaque,
		Transparent,
		PostProcessMaskedUI,
		ScreenUI,
		EditorOverlay,
		Count
	};

	// Count は実描画フェーズではなく番兵として扱う
	static constexpr size_t kRenderPhaseCount = static_cast<size_t>(RenderPhase::Count);

	// JSON保存やデバッグ表示に使う正式名称を返す
	std::string_view ToString(RenderPhase phase);
	// 文字列から描画フェーズへ変換する
	bool TryParseRenderPhase(std::string_view value, RenderPhase& outPhase);
	// 不正な文字列なら fallback を返す
	RenderPhase RenderPhaseFromString(std::string_view value, RenderPhase fallback = RenderPhase::Opaque);
} // Engine
