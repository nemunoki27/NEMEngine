#include "DayNightCycleComponent.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

	// 日付境界を越えた時刻を一日へ戻す
	double WrapHour(double hour) {

		const double result = std::fmod(hour, 24.0);
		return result < 0.0 ? result + 24.0 : result;
	}

	// InspectorとC#で同じ範囲を使用する
	bool IsValid(const Engine::DayNightCycleComponent& component) {

		return std::isfinite(component.startHour) && std::isfinite(component.dayDurationSeconds) &&
			component.dayDurationSeconds > 0.0f && std::isfinite(component.orbitAzimuth) &&
			std::isfinite(component.noonAltitude) && component.noonAltitude > 0.0f && component.noonAltitude <= 90.0f &&
			std::isfinite(component.moonTimeOffset);
	}
}

void Engine::ApplyDayNightSettings(const DayNightCycleComponent& source, DayNightCycleComponent& target) {

	if (!IsValid(source)) throw std::invalid_argument("DayNightCycleの時刻・軌道設定が不正です");
	target.enabled = source.enabled;
	target.fixedTime = source.fixedTime;
	target.playInEditMode = source.playInEditMode;
	target.startHour = static_cast<float>(WrapHour(source.startHour));
	target.dayDurationSeconds = source.dayDurationSeconds;
	target.orbitAzimuth = source.orbitAzimuth;
	target.noonAltitude = source.noonAltitude;
	target.moonTimeOffset = source.moonTimeOffset;
	target.sunEntity = source.sunEntity;
	target.moonEntity = source.moonEntity;
	target.atmosphereEntity = source.atmosphereEntity;
	target.timeSource = source.timeSource;
}

void Engine::to_json(nlohmann::json& out, const DayNightCycleComponent& component) {

	out = {{"enabled", component.enabled}, {"fixedTime", component.fixedTime}, {"playInEditMode", component.playInEditMode},
		{"startHour", component.startHour}, {"dayDurationSeconds", component.dayDurationSeconds},
		{"orbitAzimuth", component.orbitAzimuth}, {"noonAltitude", component.noonAltitude},
		{"moonTimeOffset", component.moonTimeOffset}, {"sunEntity", component.sunEntity},
		{"moonEntity", component.moonEntity}, {"atmosphereEntity", component.atmosphereEntity}, {"timeSource", component.timeSource}};
}

void Engine::from_json(const nlohmann::json& in, DayNightCycleComponent& component) {

	DayNightCycleComponent candidate;
	candidate.enabled = in.value("enabled", true);
	candidate.fixedTime = in.value("fixedTime", false);
	candidate.playInEditMode = in.value("playInEditMode", false);
	candidate.startHour = in.value("startHour", 12.0f);
	candidate.dayDurationSeconds = in.value("dayDurationSeconds", 1200.0f);
	candidate.orbitAzimuth = in.value("orbitAzimuth", 0.0f);
	candidate.noonAltitude = in.value("noonAltitude", 60.0f);
	candidate.moonTimeOffset = in.value("moonTimeOffset", 12.0f);
	candidate.sunEntity = in.value("sunEntity", SceneEntityReference{});
	candidate.moonEntity = in.value("moonEntity", SceneEntityReference{});
	candidate.atmosphereEntity = in.value("atmosphereEntity", SceneEntityReference{});
	candidate.timeSource = in.value("timeSource", TimeSourceSettings{TimeSourceMode::Global});
	PreserveTimeSourceRuntime(component.timeSource, candidate.timeSource);
	ApplyDayNightSettings(candidate, component);
}

void Engine::DeserializeComponent([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, DayNightCycleComponent& component) {

	from_json(in, component);
}

Engine::Vector3 Engine::EvaluateDayNightDirection(double hour, float azimuth, float altitude) {

	constexpr double radians = 3.14159265358979323846 / 180.0;
	const double phase = (WrapHour(hour) - 6.0) * 3.14159265358979323846 / 12.0;
	const double tilt = altitude * radians;
	const double angle = azimuth * radians;
	const double x = std::cos(phase);
	const double y = std::sin(phase) * std::sin(tilt);
	const double z = std::sin(phase) * std::cos(tilt);
	return Vector3(static_cast<float>(-x * std::cos(angle) + z * std::sin(angle)), static_cast<float>(-y),
		static_cast<float>(-x * std::sin(angle) - z * std::cos(angle))).Normalize();
}

void Engine::UpdateDayNightClock(DayNightCycleComponent& component, double deltaTime, bool advance) {

	if (!IsValid(component)) {
		component.runtimeDiagnostic = "一日の長さは0より大きく、正午高度は0より大きい90度以下にしてください";
		return;
	}
	const int32_t request = std::exchange(component.runtimeRequest, 0);
	if (!component.runtimeInitialized || component.runtimeConfiguredStart != component.startHour || request == 1 || request == 5) {
		component.runtimeInitialized = true;
		component.runtimeHour = WrapHour(component.startHour);
		component.runtimeConfiguredStart = component.startHour;
		component.runtimePaused = false;
		component.runtimePlaying = request != 5;
		deltaTime = 0.0;
	}
	if (request == 2) component.runtimePaused = true;
	if (request == 3) { component.runtimePaused = false; component.runtimePlaying = true; }
	if (request == 4) { component.runtimePlaying = false; component.runtimePaused = false; }
	if (request == 6 && std::isfinite(component.runtimeSeekHour)) {
		component.runtimeHour = WrapHour(component.runtimeSeekHour);
		deltaTime = 0.0;
	}
	if (component.fixedTime) component.runtimeHour = WrapHour(component.startHour);
	else if (advance && component.runtimePlaying && !component.runtimePaused && std::isfinite(deltaTime) && deltaTime > 0.0)
		component.runtimeHour = WrapHour(component.runtimeHour + deltaTime * 24.0 / component.dayDurationSeconds);
	component.runtimeCurrentHour = static_cast<float>(component.runtimeHour);
	component.runtimeSunDirection = EvaluateDayNightDirection(component.runtimeHour, component.orbitAzimuth, component.noonAltitude);
	component.runtimeMoonDirection = EvaluateDayNightDirection(component.runtimeHour - component.moonTimeOffset,
		component.orbitAzimuth, component.noonAltitude);
}
