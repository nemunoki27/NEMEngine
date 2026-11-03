#include "TimerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

	// スクリプトからの変更も同じ条件で検証する
	bool IsValid(const Engine::TimerComponent& component) {

		return (component.mode == Engine::TimerMode::Seconds || component.mode == Engine::TimerMode::Frames) &&
			std::isfinite(component.targetSeconds) && component.targetSeconds >= 0 && component.targetFrames >= 0 &&
			std::isfinite(component.baseFPS) && component.baseFPS > 0;
	}

	// 小数フレームを保持したまま公開値を更新する
	void Publish(Engine::TimerComponent& component) {

		component.runtimeSeconds = static_cast<float>(std::min(component.runtimeElapsed,
			static_cast<double>(std::numeric_limits<float>::max())));
		const double frames = component.runtimeElapsed * component.baseFPS;
		component.runtimeFrames = static_cast<int32_t>(std::clamp(std::floor(frames + 1.0e-6), 0.0,
			static_cast<double>(std::numeric_limits<int32_t>::max())));
	}
}

void Engine::ApplyTimerSettings(const TimerComponent& source, TimerComponent& target) {

	if (!IsValid(source)) throw std::invalid_argument("Timerの時間設定が不正です");
	target.enabled = source.enabled;
	target.playOnStart = source.playOnStart;
	target.playInEditMode = source.playInEditMode;
	target.mode = source.mode;
	target.targetSeconds = source.targetSeconds;
	target.targetFrames = source.targetFrames;
	target.baseFPS = source.baseFPS;
	target.timeSource = source.timeSource;
}

void Engine::to_json(nlohmann::json& out, const TimerComponent& component) {

	out = {{"enabled", component.enabled}, {"playOnStart", component.playOnStart},
		{"playInEditMode", component.playInEditMode}, {"mode", EnumAdapter<TimerMode>::ToString(component.mode)},
		{"targetSeconds", component.targetSeconds}, {"targetFrames", component.targetFrames},
		{"baseFPS", component.baseFPS}, {"timeSource", component.timeSource}};
}

void Engine::from_json(const nlohmann::json& in, TimerComponent& component) {

	TimerComponent candidate;
	candidate.enabled = in.value("enabled", true);
	candidate.playOnStart = in.value("playOnStart", false);
	candidate.playInEditMode = in.value("playInEditMode", false);
	const auto mode = EnumAdapter<TimerMode>::FromString(in.value("mode", "Seconds"));
	if (!mode) throw std::invalid_argument("Timerの計測方式が不正です");
	candidate.mode = *mode;
	candidate.targetSeconds = in.value("targetSeconds", 1.0f);
	candidate.targetFrames = in.value("targetFrames", 60);
	candidate.baseFPS = in.value("baseFPS", 60.0f);
	candidate.timeSource = in.value("timeSource", TimeSourceSettings{});
	PreserveTimeSourceRuntime(component.timeSource, candidate.timeSource);
	ApplyTimerSettings(candidate, component);
}

void Engine::DeserializeComponent([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, TimerComponent& component) {

	// ECSの再適用でも現在値と再生操作を維持する
	from_json(in, component);
}

void Engine::UpdateTimer(TimerComponent& component, double deltaTime) {

	const int32_t request = std::exchange(component.runtimeRequest, 0);
	if (!component.runtimeInitialized) {
		component.runtimeInitialized = true;
		component.runtimePlaying = component.playOnStart;
	}
	if (request == 1 || request == 5) {
		component.runtimeElapsed = 0;
		component.runtimeSeconds = 0;
		component.runtimeFrames = 0;
		component.runtimeReached = false;
		component.runtimePaused = false;
		component.runtimePlaying = request == 1;
	} else if (request == 2 && component.runtimePlaying) {
		component.runtimePaused = true;
	} else if (request == 3 && !component.runtimeReached) {
		component.runtimePaused = false;
		component.runtimePlaying = true;
	} else if (request == 4) {
		component.runtimePlaying = false;
		component.runtimePaused = false;
	}
	if (!IsValid(component)) {
		component.runtimeDiagnostic = "目標時間は0以上、基準FPSは0より大きい値にしてください";
		return;
	}
	component.runtimeDiagnostic.clear();
	if (component.runtimePlaying && !component.runtimePaused) {
		const double target = component.mode == TimerMode::Seconds ? component.targetSeconds :
			static_cast<double>(component.targetFrames) / component.baseFPS;
		if (std::isfinite(deltaTime) && deltaTime > 0) component.runtimeElapsed += deltaTime;
		const double tolerance = std::min(1.0e-7, target * 1.0e-7);
		if (component.runtimeElapsed + tolerance >= target) {
			component.runtimeElapsed = target;
			component.runtimeReached = true;
			component.runtimePlaying = false;
		}
	}
	Publish(component);
}
