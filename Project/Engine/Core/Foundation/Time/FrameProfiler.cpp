#include "FrameProfiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <numeric>

//============================================================================
//	FrameProfiler classMethods
//============================================================================
Engine::FrameProfiler& Engine::FrameProfiler::GetInstance() {

	static FrameProfiler instance;
	return instance;
}

void Engine::FrameProfiler::BeginFrame(float deltaTimeSec, float totalTimeSec) {

	deltaTimeSec_ = deltaTimeSec;
	totalTimeSec_ = totalTimeSec;

	// 前フレームの累積を履歴へ確定し、今フレームの累積をリセットする
	for (Measure& measure : measures_) {

		if (!firstFrame_) {

			measure.samples.emplace_back(measure.accumulator);
			if (kSmoothingSample < measure.samples.size()) {
				measure.samples.erase(measure.samples.begin());
			}
		}
		measure.accumulator = 0.0f;
	}
	firstFrame_ = false;
}

void Engine::FrameProfiler::AddSample(Category category, float milliseconds) {

	const size_t index = static_cast<size_t>(category);
	if (index >= measures_.size()) {
		return;
	}
	measures_[index].accumulator += milliseconds;
}

void Engine::FrameProfiler::SetGPUPassTimes(const std::vector<NamedTime>& passes) {

	gpuPassTimes_ = passes;
}

void Engine::FrameProfiler::SetEcsSystemTimes(const std::vector<NamedTime>& systems) {

	ecsSystemTimes_ = systems;
}

float Engine::FrameProfiler::GetAverageMs(Category category) const {

	const size_t index = static_cast<size_t>(category);
	if (index >= measures_.size()) {
		return 0.0f;
	}

	const Measure& measure = measures_[index];
	// 確定済み履歴がなければ、計測中の累積値をそのまま返す
	if (measure.samples.empty()) {
		return measure.accumulator;
	}
	const float sum = std::accumulate(measure.samples.begin(), measure.samples.end(), 0.0f);
	return sum / static_cast<float>(measure.samples.size());
}

float Engine::FrameProfiler::GetGPUTotalMs() const {

	float total = 0.0f;
	for (const GPUPassTime& pass : gpuPassTimes_) {
		total += pass.milliseconds;
	}
	return total;
}
