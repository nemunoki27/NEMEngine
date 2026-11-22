#pragma once

#include <Engine/Core/World/Components/Animation/TextCharAnimationComponent.h>

namespace Engine {

	// 描画されるグリフの順序から開始時刻と一巡の長さを求める
	void UpdateTextCharSchedule(TextCharAnimationSchedule& schedule, const TextCharAnimationSettings& settings,
		std::span<const TextLayoutGlyph> glyphs, uint64_t layoutRevision, float glyphDuration);
}
