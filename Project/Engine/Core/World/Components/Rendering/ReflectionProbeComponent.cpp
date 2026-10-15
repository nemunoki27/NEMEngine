#include "ReflectionProbeComponent.h"

//============================================================================
//	ReflectionProbeComponent functions
//============================================================================
void Engine::from_json(const nlohmann::json& in, ReflectionProbeComponent& component) {

	component.shape = in.value("shape", 0u);
	component.radius = in.value("radius", 10.0f);
	if (in.contains("extents")) component.extents = Vector3::FromJson(in.at("extents"));
	component.intensity = in.value("intensity", 1.0f);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const ReflectionProbeComponent& component) {

	out = {{"shape", component.shape}, {"radius", component.radius}, {"extents", component.extents.ToJson()},
		{"intensity", component.intensity}, {"enabled", component.enabled}};
}
