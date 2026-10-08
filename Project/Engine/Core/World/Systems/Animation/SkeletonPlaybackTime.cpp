#include "SkeletonPlaybackTime.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <limits>

float Engine::SkeletonPlaybackTime::Advance(
	float time, float duration, float delta, bool loop, int32_t& repeatCount, bool& finished) {

	if (!std::isfinite(time) || !std::isfinite(delta) || !std::isfinite(duration) || duration <= 0.0f) {
		return 0.0f;
	}
	if (delta == 0.0f) {
		return std::clamp(time, 0.0f, duration);
	}
	const double next = static_cast<double>(time) + delta;
	// 非ループは再生方向の終端で停止する
	if (!loop) {

		finished = delta > 0.0f ? next >= duration : delta < 0.0f ? next <= 0.0 : finished;
		return static_cast<float>(std::clamp(next, 0.0, static_cast<double>(duration)));
	}
	// 複数周回と逆再生でも負の評価時刻を作らない
	const double cycles = delta > 0.0f ? std::floor(next / duration) - std::floor(static_cast<double>(time) / duration)
									   : std::ceil(static_cast<double>(time) / duration) - std::ceil(next / duration);
	repeatCount = static_cast<int32_t>(
		(std::min)(static_cast<double>(repeatCount) + cycles, static_cast<double>((std::numeric_limits<int32_t>::max)())));
	// 周回後の時刻をClip内へ戻す
	double phase = std::fmod(next, duration);
	if (phase < 0.0) {
		phase += duration;
	}
	finished = false;
	return static_cast<float>(phase);
}
