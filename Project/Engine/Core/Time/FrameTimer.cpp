#include "FrameTimer.h"

//============================================================================
//	FrameTimer classMethods
//============================================================================

void Engine::FrameTimer::Init() {

	deltaTime_ = 0.0f;
	startTime_ = std::chrono::steady_clock::now();
	preFrameTime_ = std::chrono::steady_clock::now();
}

void Engine::FrameTimer::Update() {

	auto currentFrameTime = std::chrono::steady_clock::now();
	std::chrono::duration<float> elapsedTime = currentFrameTime - preFrameTime_;

	// フレーム時間を更新
	deltaTime_ = elapsedTime.count();

	// 前フレームの時間を更新
	preFrameTime_ = currentFrameTime;
}

void Engine::FrameTimer::ResetDeltaTimeBase() {

	deltaTime_ = 0.0f;
	preFrameTime_ = std::chrono::steady_clock::now();
}

void Engine::FrameTimer::BeginUpdateCount() {

	// 計測開始
	BeginMeasure(updateMeasure_);
}

void Engine::FrameTimer::EndUpdateCount() {

	// 計測終了
	EndMeasure(updateMeasure_);
}

void Engine::FrameTimer::BeginDrawCount() {

	// 計測開始
	BeginMeasure(drawMeasure_);
}

void Engine::FrameTimer::EndDrawCount() {

	// 計測終了
	EndMeasure(drawMeasure_);
}

float Engine::FrameTimer::GetTotalTime() const {

	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<float> elapsed = now - startTime_;
	return elapsed.count();
}

float Engine::FrameTimer::GetAverageTime(const Measurement& measure) const {

	// 計測結果がない場合は0を返す
	if (measure.times.empty()) {
		return 0.0f;
	}
	float sum = std::accumulate(measure.times.begin(), measure.times.end(), 0.0f);
	return sum / measure.times.size();
}

void Engine::FrameTimer::BeginMeasure(Measurement& measure) {

	// 計測開始
	measure.start = std::chrono::high_resolution_clock::now();
}

void Engine::FrameTimer::EndMeasure(Measurement& measure) {

	// 平均化するサンプル数
	const size_t kSmoothingSample = 8;

	// 計測終了
	measure.end = std::chrono::high_resolution_clock::now();
	measure.resultSeconds = measure.end - measure.start;

	// 計測結果を保存
	measure.times.emplace_back(measure.resultSeconds.count());
	// 平均化するサンプル数を超えたら古いデータを削除
	if (kSmoothingSample < measure.times.size()) {

		// 古いデータを削除
		measure.times.erase(measure.times.begin());
	}
}
