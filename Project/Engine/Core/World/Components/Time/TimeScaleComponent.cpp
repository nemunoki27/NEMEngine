#include "TimeScaleComponent.h"

//============================================================================
//	TimeScaleComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, TimeScaleComponent& component) {

	component.timeScale = in.value("timeScale", 1.0f);
}

void Engine::to_json(nlohmann::json& out, const TimeScaleComponent& component) {

	out["timeScale"] = component.timeScale;
}
