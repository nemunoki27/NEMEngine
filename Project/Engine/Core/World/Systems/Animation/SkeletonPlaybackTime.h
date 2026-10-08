#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine::SkeletonPlaybackTime {

	// 正逆再生の終端と周回数を更新する
	float Advance(float time, float duration, float delta, bool loop, int32_t& repeatCount, bool& finished);
}
