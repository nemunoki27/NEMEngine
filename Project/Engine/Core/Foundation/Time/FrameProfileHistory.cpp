#include "FrameProfileHistory.h"

//============================================================================
//	include
//============================================================================
// c++
#include <numeric>

void Engine::FrameProfileHistory::BeginFrame(bool firstFrame) {

	if (!firstFrame) {
		samples_.emplace_back(accumulator_);
		if (8 < samples_.size()) {
			samples_.erase(samples_.begin());
		}
	}
	accumulator_ = 0.0f;
}

float Engine::FrameProfileHistory::GetAverage() const {

	if (samples_.empty()) {
		return accumulator_;
	}
	return std::accumulate(samples_.begin(), samples_.end(), 0.0f) / static_cast<float>(samples_.size());
}
