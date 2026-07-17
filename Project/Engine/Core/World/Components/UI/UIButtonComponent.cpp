#include "UIButtonComponent.h"

//============================================================================
//	UIButtonComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, UIButtonComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.actionName = in.value("actionName", component.actionName);
}

void Engine::to_json(nlohmann::json& out, const UIButtonComponent& component) {

	out["enabled"] = component.enabled;
	out["actionName"] = component.actionName;
}
