#include "CameraControllerComponent.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	CameraControllerComponent classMethods
//============================================================================

namespace {

	// jsonからVector3を読み込む
	Engine::Vector3 ReadVector3(const nlohmann::json& in, const char* key, const Engine::Vector3& fallback) {

		if (!in.contains(key)) {
			return fallback;
		}
		return Engine::Vector3::FromJson(in.value(key, nlohmann::json{}));
	}

	// 追従設定をjsonから読み込む
	void ReadFollowSettings(const nlohmann::json& in, Engine::CameraFollowSettings& settings) {

		settings.enabled = in.value("enabled", settings.enabled);
		settings.target = Engine::FromString16Hex(in.value("target", std::string{}));
		settings.offset = ReadVector3(in, "offset", settings.offset);
		settings.axisMask = ReadVector3(in, "axisMask", settings.axisMask);
		settings.positionLerpSpeed = in.value("positionLerpSpeed", settings.positionLerpSpeed);
		settings.useBounds = in.value("useBounds", settings.useBounds);
		settings.boundsMin = ReadVector3(in, "boundsMin", settings.boundsMin);
		settings.boundsMax = ReadVector3(in, "boundsMax", settings.boundsMax);
	}

	// 注視設定をjsonから読み込む
	void ReadLookAtSettings(const nlohmann::json& in, Engine::CameraLookAtSettings& settings) {

		settings.enabled = in.value("enabled", settings.enabled);
		settings.target = Engine::FromString16Hex(in.value("target", std::string{}));
		settings.offset = ReadVector3(in, "offset", settings.offset);
		settings.rotationLerpSpeed = in.value("rotationLerpSpeed", settings.rotationLerpSpeed);
		settings.lockRoll = in.value("lockRoll", settings.lockRoll);
	}

	// 揺れ設定をjsonから読み込む
	void ReadShakeSettings(const nlohmann::json& in, Engine::CameraShakeSettings& settings) {

		settings.enabled = in.value("enabled", settings.enabled);
		settings.amplitude = in.value("amplitude", settings.amplitude);
		settings.duration = in.value("duration", settings.duration);
		settings.frequency = in.value("frequency", settings.frequency);
		settings.damping = in.value("damping", settings.damping);
		settings.axisMask = ReadVector3(in, "axisMask", settings.axisMask);

		// ランタイム状態はシリアライズしない
		settings.runtimeTime = 0.0f;
		settings.runtimeDuration = 0.0f;
		settings.runtimeAmplitude = 0.0f;
		settings.runtimeLastOffset = Engine::Vector3::AnyInit(0.0f);
		settings.runtimeApplied = false;
	}

	// 追従設定をjsonへ書き込む
	nlohmann::json WriteFollowSettings(const Engine::CameraFollowSettings& settings) {

		nlohmann::json out{};
		out["enabled"] = settings.enabled;
		out["target"] = Engine::ToString(settings.target);
		out["offset"] = settings.offset.ToJson();
		out["axisMask"] = settings.axisMask.ToJson();
		out["positionLerpSpeed"] = settings.positionLerpSpeed;
		out["useBounds"] = settings.useBounds;
		out["boundsMin"] = settings.boundsMin.ToJson();
		out["boundsMax"] = settings.boundsMax.ToJson();
		return out;
	}

	// 注視設定をjsonへ書き込む
	nlohmann::json WriteLookAtSettings(const Engine::CameraLookAtSettings& settings) {

		nlohmann::json out{};
		out["enabled"] = settings.enabled;
		out["target"] = Engine::ToString(settings.target);
		out["offset"] = settings.offset.ToJson();
		out["rotationLerpSpeed"] = settings.rotationLerpSpeed;
		out["lockRoll"] = settings.lockRoll;
		return out;
	}

	// 揺れ設定をjsonへ書き込む
	nlohmann::json WriteShakeSettings(const Engine::CameraShakeSettings& settings) {

		nlohmann::json out{};
		out["enabled"] = settings.enabled;
		out["amplitude"] = settings.amplitude;
		out["duration"] = settings.duration;
		out["frequency"] = settings.frequency;
		out["damping"] = settings.damping;
		out["axisMask"] = settings.axisMask.ToJson();
		return out;
	}
}

const char* Engine::ToString(CameraControlMode mode) {

	switch (mode) {
	case CameraControlMode::None:
		return "None";
	case CameraControlMode::Follow:
		return "Follow";
	case CameraControlMode::LookAt:
		return "LookAt";
	case CameraControlMode::FollowLookAt:
		return "FollowLookAt";
	default:
		return "Unknown";
	}
}

Engine::CameraControlMode Engine::CameraControlModeFromString(const std::string& text) {

	if (text == "Follow") {
		return CameraControlMode::Follow;
	}
	if (text == "LookAt") {
		return CameraControlMode::LookAt;
	}
	if (text == "FollowLookAt") {
		return CameraControlMode::FollowLookAt;
	}
	return CameraControlMode::None;
}

void Engine::RequestCameraShake(CameraControllerComponent& component,
	float amplitude, float duration, float frequency) {

	CameraShakeSettings& shake = component.shake;
	if (!shake.enabled) {
		return;
	}

	shake.runtimeTime = 0.0f;
	shake.runtimeDuration = (std::max)(0.0f, duration);
	shake.runtimeAmplitude = (std::max)(0.0f, amplitude);
	if (0.0f < frequency) {
		shake.frequency = frequency;
	}
}

void Engine::from_json(const nlohmann::json& in, CameraControllerComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.mode = CameraControlModeFromString(in.value("mode", std::string(ToString(component.mode))));

	if (in.contains("follow") && in["follow"].is_object()) {
		ReadFollowSettings(in["follow"], component.follow);
	}
	if (in.contains("lookAt") && in["lookAt"].is_object()) {
		ReadLookAtSettings(in["lookAt"], component.lookAt);
	}
	if (in.contains("shake") && in["shake"].is_object()) {
		ReadShakeSettings(in["shake"], component.shake);
	}
}

void Engine::to_json(nlohmann::json& out, const CameraControllerComponent& component) {

	out["enabled"] = component.enabled;
	out["mode"] = ToString(component.mode);
	out["follow"] = WriteFollowSettings(component.follow);
	out["lookAt"] = WriteLookAtSettings(component.lookAt);
	out["shake"] = WriteShakeSettings(component.shake);
}
