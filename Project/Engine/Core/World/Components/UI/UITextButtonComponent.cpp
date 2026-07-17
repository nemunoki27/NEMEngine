#include "UITextButtonComponent.h"

//============================================================================
//	UITextButtonComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, UITextButtonComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.actionName = in.value("actionName", component.actionName);
}

void Engine::to_json(nlohmann::json& out, const UITextButtonComponent& component) {

	out["enabled"] = component.enabled;
	out["actionName"] = component.actionName;
}
