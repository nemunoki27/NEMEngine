#include "SimpleAnimationEvaluation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <array>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace {

	constexpr std::array<const char*, 8> kTypes{"Float", "Vector2", "Vector3", "Vector4", "Color3", "Color4", "Quaternion", "Int"};
	constexpr std::array<const char*, 3> kModes{"Interpolate", "Sin", "Cos"};
	constexpr std::array<const char*, 3> kRepeats{"Once", "Loop", "PingPong"};
	constexpr std::array<const char*, 2> kRotations{"AxisAngle", "Euler"};

	// 保存名を列挙値に戻す
	template <size_t N>
	size_t ReadIndex(const std::array<const char*, N>& names, const std::string& name) {

		for (size_t i = 0; i < names.size(); ++i) if (name == names[i]) return i;
		throw std::invalid_argument("SimpleAnimationの種類が不正です");
	}

	// 値の型を保持して保存する
	nlohmann::json WriteValue(const Engine::SimpleAnimationValue& value) {

		return std::visit([](const auto& typed) -> nlohmann::json {
			if constexpr (std::is_arithmetic_v<std::decay_t<decltype(typed)>>) return typed;
			else return typed.ToJson();
		}, value);
	}

	// 選択済みの型として値を読む
	void ReadValue(const nlohmann::json& json, Engine::SimpleAnimationValue& value) {

		std::visit([&](auto& typed) {
			using T = std::decay_t<decltype(typed)>;
			if constexpr (std::is_arithmetic_v<T>) typed = json.get<T>();
			else typed = T::FromJson(json);
		}, value);
	}

	// 全成分が有限値かを確認する
	bool FiniteValue(const Engine::SimpleAnimationValue& value) {

		const auto json = WriteValue(value);
		if (json.is_number()) return std::isfinite(json.get<double>());
		for (const auto& field : json) if (!field.is_number() || !std::isfinite(field.get<double>())) return false;
		return true;
	}
}

Engine::SimpleAnimationSettings Engine::MakeSimpleAnimationSettings(size_t type) {

	SimpleAnimationSettings settings;
	switch (type) {
	case 0: break;
	case 1: settings.start = settings.center = Vector2{}; settings.end = settings.amplitude = Vector2(1, 1); break;
	case 2: settings.start = settings.center = Vector3{}; settings.end = settings.amplitude = Vector3(1, 1, 1); break;
	case 3: settings.start = settings.center = Vector4(0, 0, 0, 0); settings.end = settings.amplitude = Vector4(1, 1, 1, 1); break;
	case 4: settings.start = settings.center = Color3(0, 0, 0); settings.end = settings.amplitude = Color3(1, 1, 1); break;
	case 5: settings.start = settings.center = Color4(0, 0, 0, 0); settings.end = settings.amplitude = Color4(1, 1, 1, 1); break;
	case 6: settings.start = settings.end = settings.center = settings.amplitude = Quaternion::Identity(); break;
	case 7: settings.start = settings.center = int32_t(0); settings.end = settings.amplitude = int32_t(1); break;
	default: throw std::invalid_argument("SimpleAnimationの型が不正です");
	}
	return settings;
}

bool Engine::IsValidSimpleAnimation(const SimpleAnimationSettings& settings) {

	const size_t type = settings.start.index();
	return settings.end.index() == type && settings.center.index() == type && settings.amplitude.index() == type &&
		static_cast<size_t>(settings.rotationMode) < kRotations.size() &&
		FiniteValue(settings.startEuler) && FiniteValue(settings.endEuler) && FiniteValue(settings.centerEuler) &&
		static_cast<size_t>(settings.mode) < kModes.size() && static_cast<size_t>(settings.repeat) < kRepeats.size() &&
		FiniteValue(settings.start) && FiniteValue(settings.end) && FiniteValue(settings.center) && FiniteValue(settings.amplitude) &&
		FiniteValue(settings.angularAmplitude) && std::isfinite(settings.duration) && settings.duration >= 0 &&
		std::isfinite(settings.period) && settings.period > 0 && std::isfinite(settings.delay) && settings.delay >= 0 &&
		std::isfinite(settings.speed) && settings.speed >= 0 && std::isfinite(settings.phaseDegrees) &&
		std::isfinite(settings.blendDuration) && settings.blendDuration >= 0;
}

void Engine::to_json(nlohmann::json& out, const SimpleAnimationSettings& settings) {

	out = {{"type", kTypes.at(settings.start.index())}, {"mode", kModes.at(static_cast<size_t>(settings.mode))},
		{"repeat", kRepeats.at(static_cast<size_t>(settings.repeat))}, {"start", WriteValue(settings.start)},
		{"cycleCount", settings.cycleCount},
		{"rotationMode", kRotations.at(static_cast<size_t>(settings.rotationMode))},
		{"startEuler", settings.startEuler.ToJson()}, {"endEuler", settings.endEuler.ToJson()}, {"centerEuler", settings.centerEuler.ToJson()},
		{"end", WriteValue(settings.end)}, {"center", WriteValue(settings.center)}, {"amplitude", WriteValue(settings.amplitude)},
		{"angularAmplitude", settings.angularAmplitude.ToJson()}, {"easing", EnumAdapter<EasingType>::ToString(settings.easing)},
		{"duration", settings.duration}, {"period", settings.period}, {"phaseDegrees", settings.phaseDegrees},
		{"delay", settings.delay}, {"speed", settings.speed}, {"unscaledTime", settings.unscaledTime},
		{"startFromCurrent", settings.startFromCurrent}, {"blendDuration", settings.blendDuration}};
}

void Engine::from_json(const nlohmann::json& in, SimpleAnimationSettings& settings) {

	auto result = MakeSimpleAnimationSettings(ReadIndex(kTypes, in.value("type", "Float")));
	result.mode = static_cast<SimpleAnimationMode>(ReadIndex(kModes, in.value("mode", "Interpolate")));
	result.repeat = static_cast<SimpleAnimationRepeat>(ReadIndex(kRepeats, in.value("repeat", "Once")));
	result.cycleCount = in.value("cycleCount", uint32_t(0));
	result.rotationMode = static_cast<SimpleAnimationRotationMode>(ReadIndex(kRotations, in.value("rotationMode", "AxisAngle")));
	if (in.contains("startEuler")) result.startEuler = Vector3::FromJson(in.at("startEuler"));
	if (in.contains("endEuler")) result.endEuler = Vector3::FromJson(in.at("endEuler"));
	if (in.contains("centerEuler")) result.centerEuler = Vector3::FromJson(in.at("centerEuler"));
	if (in.contains("start")) ReadValue(in.at("start"), result.start);
	if (in.contains("end")) ReadValue(in.at("end"), result.end);
	if (in.contains("center")) ReadValue(in.at("center"), result.center);
	if (in.contains("amplitude")) ReadValue(in.at("amplitude"), result.amplitude);
	if (in.contains("angularAmplitude")) result.angularAmplitude = Vector3::FromJson(in.at("angularAmplitude"));
	result.easing = EnumAdapter<EasingType>::FromString(in.value("easing", "Linear")).value_or(EasingType::Linear);
	result.duration = in.value("duration", 1.0); result.period = in.value("period", 1.0);
	result.phaseDegrees = in.value("phaseDegrees", 0.0); result.delay = in.value("delay", 0.0);
	result.speed = in.value("speed", 1.0); result.unscaledTime = in.value("unscaledTime", false);
	result.startFromCurrent = in.value("startFromCurrent", false); result.blendDuration = in.value("blendDuration", 0.0);
	if (!IsValidSimpleAnimation(result)) throw std::invalid_argument("SimpleAnimationの値が不正です");
	settings = std::move(result);
}
