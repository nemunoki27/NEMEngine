#include "UIProgressComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	UIProgressComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, UIProgressComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.minValue = in.value("minValue", component.minValue);
	component.maxValue = in.value("maxValue", component.maxValue);
	component.value = in.value("value", component.value);
	component.delayedTargetLocalFileID = UIComponentSerialization::ReadEntityReference(in, "delayedTarget");
	component.direction = EnumAdapter<UIProgressFillDirection>::FromString(
		in.value("direction", "LeftToRight")).value_or(component.direction);
	component.smooth = in.value("smooth", component.smooth);
	component.smoothDuration = in.value("smoothDuration", component.smoothDuration);
	component.smoothEasing = EnumAdapter<EasingType>::FromString(
		in.value("smoothEasing", "EaseOutSine")).value_or(component.smoothEasing);
	component.delayed = in.value("delayed", component.delayed);
	component.delayedWait = in.value("delayedWait", component.delayedWait);
	component.delayedDuration = in.value("delayedDuration", component.delayedDuration);
	component.delayedEasing = EnumAdapter<EasingType>::FromString(
		in.value("delayedEasing", "EaseOutSine")).value_or(component.delayedEasing);
	component.useUnscaledTime = in.value("useUnscaledTime", component.useUnscaledTime);
	ResetUIProgressRuntime(component);
}

void Engine::to_json(nlohmann::json& out, const UIProgressComponent& component) {

	out["enabled"] = component.enabled;
	out["minValue"] = component.minValue;
	out["maxValue"] = component.maxValue;
	out["value"] = component.value;
	out["delayedTarget"] = UIComponentSerialization::WriteEntityReference(component.delayedTargetLocalFileID);
	out["direction"] = EnumAdapter<UIProgressFillDirection>::ToString(component.direction);
	out["smooth"] = component.smooth;
	out["smoothDuration"] = component.smoothDuration;
	out["smoothEasing"] = EnumAdapter<EasingType>::ToString(component.smoothEasing);
	out["delayed"] = component.delayed;
	out["delayedWait"] = component.delayedWait;
	out["delayedDuration"] = component.delayedDuration;
	out["delayedEasing"] = EnumAdapter<EasingType>::ToString(component.delayedEasing);
	out["useUnscaledTime"] = component.useUnscaledTime;
}

void Engine::ResetUIProgressRuntime(UIProgressComponent& component) {

	component.runtimeDisplayedValue = component.value;
	component.runtimeDelayedValue = component.value;
	component.runtimeDisplayStart = component.value;
	component.runtimeDelayedStart = component.value;
	component.runtimeTargetValue = component.value;
	component.runtimeSmoothElapsed = 0.0f;
	component.runtimeDelayedElapsed = 0.0f;
	component.runtimeFillTarget = {};
	component.runtimeDelayedTarget = {};
	component.runtimeInitialized = false;
}
