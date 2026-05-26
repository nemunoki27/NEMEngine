#include "NameComponent.h"

//============================================================================
//	NameComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, NameComponent& component) {

	component.name = in.value("name", "Entity");
}

void Engine::to_json(nlohmann::json& out, const NameComponent& component) {

	out["name"] = component.name;
}