//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>

//============================================================================
//	DirectionalLightComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, DirectionalLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.direction = Vector3::FromJson(in.value("direction", nlohmann::json()));
	component.intensity = in.value("intensity", 10.0f);
	component.shadowStrength = in.value("shadowStrength", 0.92f);
	component.shadowAngularRadius =
		in.value("shadowAngularRadius", component.shadowAngularRadius);
	component.enabled = in.value("enabled", true);
	component.affectLayerMask = in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const DirectionalLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["direction"] = component.direction.ToJson();
	out["intensity"] = component.intensity;
	out["shadowStrength"] = component.shadowStrength;
	out["shadowAngularRadius"] = component.shadowAngularRadius;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}

//============================================================================
//	PointLightComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, PointLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.intensity = in.value("intensity", 1.0f);
	component.radius = in.value("radius", 8.0f);
	component.decay = in.value("decay", 1.0f);
	component.shadowStrength = in.value("shadowStrength", component.shadowStrength);
	component.shadowRadius = in.value("shadowRadius", component.shadowRadius);
	component.enabled = in.value("enabled", true);
	component.affectLayerMask = in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const PointLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["intensity"] = component.intensity;
	out["radius"] = component.radius;
	out["decay"] = component.decay;
	out["shadowStrength"] = component.shadowStrength;
	out["shadowRadius"] = component.shadowRadius;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}

//============================================================================
//	RectLightComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, RectLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.intensity = in.value("intensity", component.intensity);
	component.attenuationRadius =
		in.value("attenuationRadius", component.attenuationRadius);
	component.sourceWidth = in.value("sourceWidth", component.sourceWidth);
	component.sourceHeight = in.value("sourceHeight", component.sourceHeight);
	component.decay = in.value("decay", component.decay);
	component.barnDoorAngle =
		in.value("barnDoorAngle", component.barnDoorAngle);
	component.barnDoorLength =
		in.value("barnDoorLength", component.barnDoorLength);
	component.shadowStrength =
		in.value("shadowStrength", component.shadowStrength);
	component.enabled = in.value("enabled", component.enabled);
	component.affectLayerMask =
		in.value("affectLayerMask", component.affectLayerMask);
}

void Engine::to_json(nlohmann::json& out, const RectLightComponent& component) {

	out["color"] = component.color.ToJson();
	out["intensity"] = component.intensity;
	out["attenuationRadius"] = component.attenuationRadius;
	out["sourceWidth"] = component.sourceWidth;
	out["sourceHeight"] = component.sourceHeight;
	out["decay"] = component.decay;
	out["barnDoorAngle"] = component.barnDoorAngle;
	out["barnDoorLength"] = component.barnDoorLength;
	out["shadowStrength"] = component.shadowStrength;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}

//============================================================================
//	SpotLightComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, SpotLightComponent& component) {

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.direction = Vector3::FromJson(in.value("direction", nlohmann::json()));
	component.intensity = in.value("intensity", 1.0f);
	component.distance = in.value("distance", 10.0f);
	component.decay = in.value("decay", 1.0f);
	component.cosAngle = in.value("cosAngle", std::cos(Math::pi / 3.0f));
	component.cosFalloffStart = in.value("cosFalloffStart", std::cos(Math::pi / 6.0f));
	component.shadowStrength = in.value("shadowStrength", component.shadowStrength);
	component.shadowRadius = in.value("shadowRadius", component.shadowRadius);
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
	out["shadowStrength"] = component.shadowStrength;
	out["shadowRadius"] = component.shadowRadius;
	out["enabled"] = component.enabled;
	out["affectLayerMask"] = component.affectLayerMask;
}
