#include "ExponentialHeightFogComponent.h"

//============================================================================
//	ExponentialHeightFogComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, ExponentialHeightFogComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.renderingLayerMask = in.value("renderingLayerMask", component.renderingLayerMask);
	component.density = in.value("density", component.density);
	component.heightFalloff = in.value("heightFalloff", component.heightFalloff);
	if (in.contains("color")) component.color = Color3::FromJson(in["color"]);
	component.startDistance = in.value("startDistance", component.startDistance);
	component.maxOpacity = in.value("maxOpacity", component.maxOpacity);
	component.cutoffDistance = in.value("cutoffDistance", component.cutoffDistance);
	if (in.contains("directionalColor")) component.directionalColor = Color3::FromJson(in["directionalColor"]);
	component.directionalExponent = in.value("directionalExponent", component.directionalExponent);
	component.directionalStartDistance = in.value("directionalStartDistance", component.directionalStartDistance);
	component.directionalIntensity = in.value("directionalIntensity", component.directionalIntensity);
	component.skyContribution = in.value("skyContribution", component.skyContribution);
	component.secondDensity = in.value("secondDensity", component.secondDensity);
	component.secondHeightFalloff = in.value("secondHeightFalloff", component.secondHeightFalloff);
	component.secondHeightOffset = in.value("secondHeightOffset", component.secondHeightOffset);
	component.renderingLayerMask &= kRenderingLayerMaskBits;
}

void Engine::to_json(nlohmann::json& out, const ExponentialHeightFogComponent& component) {

	out["enabled"] = component.enabled;
	out["renderingLayerMask"] = component.renderingLayerMask;
	out["density"] = component.density;
	out["heightFalloff"] = component.heightFalloff;
	out["color"] = component.color.ToJson();
	out["startDistance"] = component.startDistance;
	out["maxOpacity"] = component.maxOpacity;
	out["cutoffDistance"] = component.cutoffDistance;
	out["directionalColor"] = component.directionalColor.ToJson();
	out["directionalExponent"] = component.directionalExponent;
	out["directionalStartDistance"] = component.directionalStartDistance;
	out["directionalIntensity"] = component.directionalIntensity;
	out["skyContribution"] = component.skyContribution;
	out["secondDensity"] = component.secondDensity;
	out["secondHeightFalloff"] = component.secondHeightFalloff;
	out["secondHeightOffset"] = component.secondHeightOffset;
}
