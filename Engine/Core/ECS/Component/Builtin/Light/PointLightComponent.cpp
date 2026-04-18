#include "PointLightComponent.h"

//============================================================================
//	PointLightComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, PointLightComponent& component) {

	component.color = Color::FromJson(in.value("color", nlohmann::json()));
	component.intensity = in.value("intensity", 1.0f);
	component.radius = in.value("radius", 8.0f);
	component.decay = in.value("decay", 1.0f);
	component.enabled = in.value("enabled", true);
	component.affectLayerMask = in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const PointLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["intensity"] = component.intensity;
	out["radius"] = component.radius;
	out["decay"] = component.decay;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}