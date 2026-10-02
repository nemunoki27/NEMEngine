#include "VolumeComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

//============================================================================
//	VolumeComponent functions
//============================================================================
void Engine::from_json(const nlohmann::json& in, VolumeComponent& component) {

	component.profile = ParseAssetReference(in, "profile", nullptr, AssetType::VolumeProfile);
	component.size = JsonAdapter::GetVector3(in, "size", component.size);
	component.priority = in.value("priority", component.priority);
	component.weight = in.value("weight", component.weight);
	component.blendDistance = in.value("blendDistance", component.blendDistance);
	component.layerMask = in.value("layerMask", component.layerMask);
	component.global = in.value("global", component.global);
	component.enabled = in.value("enabled", component.enabled);
}

void Engine::to_json(nlohmann::json& out, const VolumeComponent& component) {

	out = nlohmann::json::object();
	out["profile"] = ToAssetReferenceJson(component.profile);
	JsonAdapter::SetVector3(out, "size", component.size);
	out["priority"] = component.priority;
	out["weight"] = component.weight;
	out["blendDistance"] = component.blendDistance;
	out["layerMask"] = component.layerMask;
	out["global"] = component.global;
	out["enabled"] = component.enabled;
}
