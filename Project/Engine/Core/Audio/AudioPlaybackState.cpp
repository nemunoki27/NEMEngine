#include "AudioPlaybackState.h"

bool Engine::AudioPlaybackState::SetPauseReason(AudioPauseReason reason, bool paused) {

	// 指定理由だけを変更し、他の停止理由は保持する
	bool previous = IsPaused();
	uint8_t mask = static_cast<uint8_t>(reason);
	pauseReasons_ = paused ? pauseReasons_ | mask : pauseReasons_ & ~mask;
	return previous != IsPaused();
}
