#include "FrameProfileHistory.h"

void Engine::FrameProfileHistory::BeginFrame(bool firstFrame) {

	if (firstFrame) {

		// 計測の再開時は古い平均値も破棄する
		samples_ = {};
		nextSample_ = 0;
		sampleCount_ = 0;
	} else {

		// 最古の記録へ上書きして履歴の確保を省く
		samples_[nextSample_] = accumulator_;
		nextSample_ = (nextSample_ + 1) % kSampleCount;
		if (sampleCount_ < kSampleCount) { ++sampleCount_; }
	}
	accumulator_ = 0.0f;
}

float Engine::FrameProfileHistory::GetAverage() const {

	if (sampleCount_ == 0) {
		return accumulator_;
	}
	// 確定した順序で加算して平均の丸め方を維持する
	float sum = 0.0f;
	const size_t first = sampleCount_ == kSampleCount ? nextSample_ : 0;
	for (size_t index = 0; index < sampleCount_; ++index) {
		sum += samples_[(first + index) % kSampleCount];
	}
	return sum / static_cast<float>(sampleCount_);
}

float Engine::FrameProfileHistory::GetLatest() const {

	return sampleCount_ == 0 ? 0.0f : samples_[(nextSample_ + kSampleCount - 1) % kSampleCount];
}
