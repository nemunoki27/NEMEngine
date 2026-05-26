#include "SpotLightComponent.h"

//============================================================================
//	SpotLightComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, SpotLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.direction = Vector3::FromJson(in.value("direction", nlohmann::json()));
	component.intensity = in.value("intensity", 1.0f);
	component.distance = in.value("distance", 10.0f);
	component.decay = in.value("decay", 1.0f);
	component.cosAngle = in.value("cosAngle", std::cos(Math::pi / 3.0f));
	component.cosFalloffStart = in.value("cosFalloffStart", std::cos(Math::pi / 6.0f));
	component.enabled = in.value("enabled", true);
	component.affectLayerMask = in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const SpotLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["direction"] = component.direction.ToJson();
	out["intensity"] = component.intensity;
	out["distance"] = component.distance;
	out["decay"] = component.decay;
	out["cosAngle"] = component.cosAngle;
	out["cosFalloffStart"] = component.cosFalloffStart;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}