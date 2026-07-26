//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>

namespace {

	template <typename TComponent>
	void ReadButton(const nlohmann::json& in, TComponent& component) {

		component.enabled = in.value("enabled", component.enabled);
		component.actionName = in.value("actionName", component.actionName);
	}

	template <typename TComponent>
	void WriteButton(nlohmann::json& out, const TComponent& component) {

		out["enabled"] = component.enabled;
		out["actionName"] = component.actionName;
	}
}

//============================================================================
//	UIButtonComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, UIImageButtonComponent& component) {

	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UIImageButtonComponent& component) {

	WriteButton(out, component);
}

void Engine::from_json(const nlohmann::json& in, UITextButtonComponent& component) {

	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UITextButtonComponent& component) {

	WriteButton(out, component);
}
