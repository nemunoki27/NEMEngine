#include "SkyAtmosphereComponent.h"

//============================================================================
//	SkyAtmosphereComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, SkyAtmosphereComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.renderingLayerMask = in.value("renderingLayerMask", component.renderingLayerMask);
	component.sunEntity = in.value("sunEntity", component.sunEntity);
	component.planetRadiusKm = in.value("planetRadiusKm", component.planetRadiusKm);
	component.atmosphereHeightKm = in.value("atmosphereHeightKm", component.atmosphereHeightKm);
	if (in.contains("rayleighScattering")) component.rayleighScattering = Color3::FromJson(in["rayleighScattering"]);
	component.rayleighScaleHeightKm = in.value("rayleighScaleHeightKm", component.rayleighScaleHeightKm);
	component.mieScattering = in.value("mieScattering", component.mieScattering);
	component.mieAbsorption = in.value("mieAbsorption", component.mieAbsorption);
	component.mieScaleHeightKm = in.value("mieScaleHeightKm", component.mieScaleHeightKm);
	component.mieAnisotropy = in.value("mieAnisotropy", component.mieAnisotropy);
	if (in.contains("ozoneAbsorption")) component.ozoneAbsorption = Color3::FromJson(in["ozoneAbsorption"]);
	component.ozoneCenterHeightKm = in.value("ozoneCenterHeightKm", component.ozoneCenterHeightKm);
	component.ozoneWidthKm = in.value("ozoneWidthKm", component.ozoneWidthKm);
	if (in.contains("groundAlbedo")) component.groundAlbedo = Color3::FromJson(in["groundAlbedo"]);
	component.iblIntensity = in.value("iblIntensity", component.iblIntensity);
	component.aerialPerspectiveDistance = in.value("aerialPerspectiveDistance", component.aerialPerspectiveDistance);
	component.aerialPerspectiveStrength = in.value("aerialPerspectiveStrength", component.aerialPerspectiveStrength);
	component.renderingLayerMask &= kRenderingLayerMaskBits;
}

void Engine::to_json(nlohmann::json& out, const SkyAtmosphereComponent& component) {

	out["enabled"] = component.enabled;
	out["renderingLayerMask"] = component.renderingLayerMask;
	out["sunEntity"] = component.sunEntity;
	out["planetRadiusKm"] = component.planetRadiusKm;
	out["atmosphereHeightKm"] = component.atmosphereHeightKm;
	out["rayleighScattering"] = component.rayleighScattering.ToJson();
	out["rayleighScaleHeightKm"] = component.rayleighScaleHeightKm;
	out["mieScattering"] = component.mieScattering;
	out["mieAbsorption"] = component.mieAbsorption;
	out["mieScaleHeightKm"] = component.mieScaleHeightKm;
	out["mieAnisotropy"] = component.mieAnisotropy;
	out["ozoneAbsorption"] = component.ozoneAbsorption.ToJson();
	out["ozoneCenterHeightKm"] = component.ozoneCenterHeightKm;
	out["ozoneWidthKm"] = component.ozoneWidthKm;
	out["groundAlbedo"] = component.groundAlbedo.ToJson();
	out["iblIntensity"] = component.iblIntensity;
	out["aerialPerspectiveDistance"] = component.aerialPerspectiveDistance;
	out["aerialPerspectiveStrength"] = component.aerialPerspectiveStrength;
}
