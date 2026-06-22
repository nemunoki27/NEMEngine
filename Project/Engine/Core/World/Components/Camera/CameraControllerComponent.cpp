#include "CameraControllerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

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

	// jsonからVector2を読み込む
	Engine::Vector2 ReadVector2(const nlohmann::json& in, const char* key, const Engine::Vector2& fallback) {

		if (!in.contains(key)) {
			return fallback;
		}
		return Engine::Vector2::FromJson(in.value(key, nlohmann::json{}));
	}

	// 追従設定をjsonから読み込む
	void ReadFollowSettings(const nlohmann::json& in, Engine::CameraFollowSettings& settings) {

		settings.enabled = in.value("enabled", settings.enabled);
		settings.target = Engine::FromString16Hex(in.value("target", std::string{}));
		settings.offset = ReadVector3(in, "offset", settings.offset);
		settings.axisMask = ReadVector3(in, "axisMask", settings.axisMask);
		settings.posLerpSpeed = in.value("posLerpSpeed", settings.posLerpSpeed);
		settings.enableInputRotation = in.value("enableInputRotation", settings.enableInputRotation);
		settings.inputLerpRate = in.value("inputLerpRate", settings.inputLerpRate);
		settings.padSensitivity = ReadVector2(in, "padSensitivity", settings.padSensitivity);
		settings.mouseSensitivity = ReadVector2(in, "mouseSensitivity", settings.mouseSensitivity);
		settings.minPitchDegrees = in.value("minPitchDegrees", settings.minPitchDegrees);
		settings.maxPitchDegrees = in.value("maxPitchDegrees", settings.maxPitchDegrees);
		settings.invertPitch = in.value("invertPitch", settings.invertPitch);
	}

	// 注視設定をjsonから読み込む
	void ReadLookAtSettings(const nlohmann::json& in, Engine::CameraLookAtSettings& settings) {

		settings.enabled = in.value("enabled", settings.enabled);
		settings.target = Engine::FromString16Hex(in.value("target", std::string{}));
		settings.offset = ReadVector3(in, "offset", settings.offset);
		settings.rotationLerpSpeed = in.value("rotationLerpSpeed", settings.rotationLerpSpeed);
		settings.lockRoll = in.value("lockRoll", settings.lockRoll);
	}

	// 追従設定をjsonへ書き込む
	nlohmann::json WriteFollowSettings(const Engine::CameraFollowSettings& settings) {

		nlohmann::json out{};
		out["enabled"] = settings.enabled;
		out["target"] = Engine::ToString(settings.target);
		out["offset"] = settings.offset.ToJson();
		out["axisMask"] = settings.axisMask.ToJson();
		out["posLerpSpeed"] = settings.posLerpSpeed;
		out["enableInputRotation"] = settings.enableInputRotation;
		out["inputLerpRate"] = settings.inputLerpRate;
		out["padSensitivity"] = settings.padSensitivity.ToJson();
		out["mouseSensitivity"] = settings.mouseSensitivity.ToJson();
		out["minPitchDegrees"] = settings.minPitchDegrees;
		out["maxPitchDegrees"] = settings.maxPitchDegrees;
		out["invertPitch"] = settings.invertPitch;
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
}

void Engine::from_json(const nlohmann::json& in, CameraControllerComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.mode = EnumAdapter<CameraControlMode>::FromString(in.value("mode", "Follow")).value_or(component.mode);
	component.editorPreview = in.value("editorPreview", component.editorPreview);

	if (in.contains("follow") && in["follow"].is_object()) {
		ReadFollowSettings(in["follow"], component.follow);
	}
	if (in.contains("lookAt") && in["lookAt"].is_object()) {
		ReadLookAtSettings(in["lookAt"], component.lookAt);
	}
	// FollowLookAtモード専用の設定を読み込む
	if (in.contains("followLookAt") && in["followLookAt"].is_object()) {

		const nlohmann::json& followLookAt = in["followLookAt"];
		if (followLookAt.contains("follow") && followLookAt["follow"].is_object()) {
			ReadFollowSettings(followLookAt["follow"], component.followLookAt.follow);
		}
		if (followLookAt.contains("lookAt") && followLookAt["lookAt"].is_object()) {
			ReadLookAtSettings(followLookAt["lookAt"], component.followLookAt.lookAt);
		}
	}
}

void Engine::to_json(nlohmann::json& out, const CameraControllerComponent& component) {

	out["enabled"] = component.enabled;
	out["mode"] = EnumAdapter<CameraControlMode>::ToString(component.mode);
	out["editorPreview"] = component.editorPreview;
	out["follow"] = WriteFollowSettings(component.follow);
	out["lookAt"] = WriteLookAtSettings(component.lookAt);
	out["followLookAt"]["follow"] = WriteFollowSettings(component.followLookAt.follow);
	out["followLookAt"]["lookAt"] = WriteLookAtSettings(component.followLookAt.lookAt);
}