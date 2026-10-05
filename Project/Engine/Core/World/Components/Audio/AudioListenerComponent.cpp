#include "AudioListenerComponent.h"

void Engine::from_json(const nlohmann::json& in, AudioListenerComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
}

void Engine::to_json(nlohmann::json& out, const AudioListenerComponent& component) {

	out["enabled"] = component.enabled;
}
