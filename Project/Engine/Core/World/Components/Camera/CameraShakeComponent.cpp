#include "CameraShakeComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	CameraShakeComponent structMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, CameraShakeComponent& component) {

	component.duration = in.value("duration", component.duration);
	component.easingType = EnumAdapter<EasingType>::FromString(in.value("easingType", "Linear")).value_or(component.easingType);
	component.strength = Vector3::FromJson(in.value("strength", nlohmann::json()));

	// ランタイム値をリセット
	component.runtimeTime = 0.0f;
	component.runtimeOffset = Vector3{};
	component.runtimeActive = false;
}

void Engine::to_json(nlohmann::json& out, const CameraShakeComponent& component) {

	out["duration"] = component.duration;
	out["easingType"] = EnumAdapter<EasingType>::ToString(component.easingType);
	out["strength"] = component.strength.ToJson();
}
