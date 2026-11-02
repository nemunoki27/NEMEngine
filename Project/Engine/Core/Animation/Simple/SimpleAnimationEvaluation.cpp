#include "SimpleAnimationEvaluation.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

namespace {

	// 整数の中間値と範囲外の値を確定する
	int32_t RoundInteger(double value) {

		return static_cast<int32_t>(std::clamp(std::round(value),
			static_cast<double>((std::numeric_limits<int32_t>::min)()),
			static_cast<double>((std::numeric_limits<int32_t>::max)())));
	}

	// 同型の値を補間する
	Engine::SimpleAnimationValue Interpolate(const Engine::SimpleAnimationValue& start,
		const Engine::SimpleAnimationValue& end, float progress) {

		return std::visit([&](const auto& from) -> Engine::SimpleAnimationValue {
			using T = std::decay_t<decltype(from)>;
			const auto& to = std::get<T>(end);
			if constexpr (std::is_same_v<T, int32_t>) {
				return RoundInteger(static_cast<double>(from) + (static_cast<double>(to) - from) * progress);
			} else if constexpr (std::is_same_v<T, Engine::Quaternion>) {
				return Engine::Quaternion::Lerp(from.Normalize(), to.Normalize(), progress).Normalize();
			} else {
				return from + (to - from) * progress;
			}
		}, start);
	}

	// 中心値に振幅を加え、回転は中心姿勢のローカル角度として合成する
	Engine::SimpleAnimationValue Oscillate(const Engine::SimpleAnimationSettings& settings, float wave) {

		return std::visit([&](const auto& center) -> Engine::SimpleAnimationValue {
			using T = std::decay_t<decltype(center)>;
			if constexpr (std::is_same_v<T, Engine::Quaternion>) {
				const auto rotation = settings.rotationMode == Engine::SimpleAnimationRotationMode::Euler ?
					Engine::Quaternion::FromEulerDegrees(settings.centerEuler) : center.Normalize();
				return (rotation * Engine::Quaternion::FromEulerDegrees(settings.angularAmplitude * wave)).Normalize();
			} else if constexpr (std::is_same_v<T, int32_t>) {
				return RoundInteger(static_cast<double>(center) + static_cast<double>(std::get<T>(settings.amplitude)) * wave);
			} else {
				return center + std::get<T>(settings.amplitude) * wave;
			}
		}, settings.center);
	}
}

//============================================================================
//	SimpleAnimationEvaluation functions
//============================================================================
Engine::SimpleAnimationValue Engine::BlendSimpleAnimation(const SimpleAnimationValue& from,
	const SimpleAnimationValue& to, float progress) {

	return from.index() == to.index() ? Interpolate(from, to, progress) : to;
}

Engine::SimpleAnimationSample Engine::EvaluateSimpleAnimation(const SimpleAnimationSettings& settings, double time) {

	SimpleAnimationSample result;
	if (!std::isfinite(time) || !std::isfinite(settings.delay) || settings.delay < 0.0 ||
		!std::isfinite(settings.phaseDegrees)) return result;
	const bool interpolate = settings.mode == SimpleAnimationMode::Interpolate;
	if (interpolate ? settings.start.index() != settings.end.index() :
		(!std::holds_alternative<Quaternion>(settings.center) && settings.center.index() != settings.amplitude.index())) {
		return result;
	}
	const double length = interpolate ? settings.duration : settings.period;
	if (!std::isfinite(length) || length < 0.0 || (!interpolate && length == 0.0)) return result;
	const bool waiting = time < settings.delay;
	const double elapsed = (std::max)(time - settings.delay, 0.0);
	double progress = length > 0.0 ? elapsed / length : 1.0;
	if (!std::isfinite(progress)) return result;
	if (waiting) progress = 0.0;
	if (settings.repeat == SimpleAnimationRepeat::Once || length == 0.0) {
		result.finished = !waiting && progress >= 1.0;
		result.completedCycles = result.finished ? 1 : 0;
		progress = (std::min)(progress, 1.0);
	} else {
		const double cycleLength = settings.repeat == SimpleAnimationRepeat::PingPong ? 2.0 : 1.0;
		const double cycles = std::floor(progress / cycleLength);
		result.completedCycles = cycles >= static_cast<double>((std::numeric_limits<uint64_t>::max)()) ?
			(std::numeric_limits<uint64_t>::max)() : static_cast<uint64_t>(cycles);
		progress = std::fmod(progress, cycleLength);
		if (progress > 1.0) progress = 2.0 - progress;
		if (settings.cycleCount > 0 && cycles >= settings.cycleCount) {
			result.completedCycles = settings.cycleCount;
			result.finished = true;
			progress = settings.repeat == SimpleAnimationRepeat::PingPong ? 0.0 : 1.0;
		}
	}
	if (interpolate) {
		const float eased = EasedValue(settings.easing, static_cast<float>(progress));
		if (settings.start.index() == 6 && settings.rotationMode == SimpleAnimationRotationMode::Euler) {
			result.value = Quaternion::FromEulerDegrees(settings.startEuler + (settings.endEuler - settings.startEuler) * eased);
		} else {
			result.value = Interpolate(settings.start, settings.end, eased);
		}
	} else {
		const double angle = 2.0 * std::numbers::pi * progress +
			std::fmod(settings.phaseDegrees, 360.0) * std::numbers::pi / 180.0;
		const float wave = static_cast<float>(settings.mode == SimpleAnimationMode::Sin ? std::sin(angle) : std::cos(angle));
		result.value = Oscillate(settings, wave);
	}
	result.valid = true;
	return result;
}
