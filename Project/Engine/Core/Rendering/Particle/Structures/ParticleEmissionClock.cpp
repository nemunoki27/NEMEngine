#include "ParticleEmissionClock.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <limits>

uint32_t Engine::AdvanceParticleEmissionClock(float& timer, float interval, float deltaTime) {

	// 不正な時刻を蓄積しない
	if (!std::isfinite(timer) || timer < 0.0f) { timer = 0.0f; }
	if (!std::isfinite(deltaTime) || deltaTime < 0.0f) { deltaTime = 0.0f; }
	if (!std::isfinite(interval) || interval <= 0.0f) {
		timer = 0.0f;
		return 1;
	}

	// 発生済みの区間だけ引き、端数は次のframeへ持ち越す
	double elapsed = static_cast<double>(timer) + deltaTime;
	double count = std::floor(elapsed / interval);
	timer = static_cast<float>(std::fmod(elapsed, interval));
	return static_cast<uint32_t>((std::min)(count, static_cast<double>(std::numeric_limits<uint32_t>::max())));
}
