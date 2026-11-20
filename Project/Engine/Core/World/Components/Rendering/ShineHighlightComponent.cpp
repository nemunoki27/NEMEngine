#include "ShineHighlightComponent.h"

//============================================================================
//	ShineHighlightComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, ShineHighlightComponent& component) {

	component = {};
	component.enabled = in.value("enabled", component.enabled);
	component.position = in.value("position", component.position);
	if (in.contains("color")) component.color = Color4::FromJson(in["color"]);
	component.intensity = in.value("intensity", component.intensity);
	component.angle = in.value("angle", component.angle);
	component.width = in.value("width", component.width);
	component.softness = in.value("softness", component.softness);
	component.rampTexture = ParseAssetID(in, "rampTexture");
}

void Engine::to_json(nlohmann::json& out, const ShineHighlightComponent& component) {

	out["enabled"] = component.enabled;
	out["position"] = component.position;
	out["color"] = component.color.ToJson();
	out["intensity"] = component.intensity;
	out["angle"] = component.angle;
	out["width"] = component.width;
	out["softness"] = component.softness;
	out["rampTexture"] = ToAssetReferenceJson(component.rampTexture);
}
