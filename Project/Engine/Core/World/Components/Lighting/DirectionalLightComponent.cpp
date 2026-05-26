#include "DirectionalLightComponent.h"

//============================================================================
//	DirectionalLightComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, DirectionalLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.direction = Vector3::FromJson(in.value("direction", nlohmann::json()));
	component.intensity = in.value("intensity", 10.0f);
	component.shadowStrength = in.value("shadowStrength", 0.92f);
	component.enabled = in.value("enabled", true);
	component.affectLayerMask = in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const DirectionalLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["direction"] = component.direction.ToJson();
	out["intensity"] = component.intensity;
	out["shadowStrength"] = component.shadowStrength;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}