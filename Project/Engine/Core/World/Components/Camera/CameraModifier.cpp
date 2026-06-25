#include "CameraModifier.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	CameraModifier structMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, CameraModifier& component) {

	//　シェイク
	{
		component.shake.duration = in.value("shake.duration", component.shake.duration);
		component.shake.mode = EnumAdapter<CameraShakeMode>::FromString(in.value("shake.mode", "Linear")).value();
		component.shake.easingType = EnumAdapter<EasingType>::FromString(in.value("shake.easingType", "Linear")).value();
		component.shake.strength = Vector3::FromJson(in.value("shake.strength", nlohmann::json()));

		// ランタイム値をリセット
		component.shake.runtimeTime = 0.0f;
	}
	// 回転制限
	{
		component.pitchControl.enable = in.value("pitchControl.enable", component.pitchControl.enable);
		component.pitchControl.minPitch = in.value("pitchControl.minPitch", component.pitchControl.minPitch);
		component.pitchControl.maxPitch = in.value("pitchControl.maxPitch", component.pitchControl.maxPitch);
	}
}

void Engine::to_json(nlohmann::json& out, const CameraModifier& component) {

	// シェイク
	{
		out["shake.duration"] = component.shake.duration;
		out["shake.mode"] = EnumAdapter<CameraShakeMode>::ToString(component.shake.mode);
		out["shake.easingType"] = EnumAdapter<EasingType>::ToString(component.shake.easingType);
		out["shake.strength"] = component.shake.strength.ToJson();
	}
	// 回転制限
	{
		out["pitchControl.enable"] = component.pitchControl.enable;
		out["pitchControl.minPitch"] = component.pitchControl.minPitch;
		out["pitchControl.maxPitch"] = component.pitchControl.maxPitch;
	}
}