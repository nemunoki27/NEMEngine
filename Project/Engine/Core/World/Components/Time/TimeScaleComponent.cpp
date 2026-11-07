#include "TimeScaleComponent.h"

//============================================================================
//	TimeScaleComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, TimeScaleComponent& component) {

	component.timeScale = in.value("timeScale", component.timeScale);
	component.enabled = in.value("enabled", component.enabled);
	component.applyToChildren = in.value("applyToChildren", component.applyToChildren);
	component.inheritParentScale = in.value("inheritParentScale", component.inheritParentScale);
	component.channels = in.value("channels", component.channels) & kTimeChannelMask;
}

void Engine::to_json(nlohmann::json& out, const TimeScaleComponent& component) {

	out["timeScale"] = component.timeScale;
	out["enabled"] = component.enabled;
	out["applyToChildren"] = component.applyToChildren;
	out["inheritParentScale"] = component.inheritParentScale;
	out["channels"] = component.channels;
}
