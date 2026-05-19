#include "PrefabLinkComponent.h"

//============================================================================
//	PrefabLinkComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, PrefabLinkComponent& component) {

	std::string prefabAsset = in.value("prefabAsset", "");
	std::string prefabLocalFileID = in.value("prefabLocalFileID", "");
	std::string prefabInstanceID = in.value("prefabInstanceID", "");

	component.prefabAsset = prefabAsset.empty() ? AssetID{} : FromString16Hex(prefabAsset);
	component.prefabLocalFileID = prefabLocalFileID.empty() ? UUID{} : FromString16Hex(prefabLocalFileID);
	component.prefabInstanceID = prefabInstanceID.empty() ? UUID{} : FromString16Hex(prefabInstanceID);
	component.isPrefabRoot = in.value("isPrefabRoot", false);
}

void Engine::to_json(nlohmann::json& out, const PrefabLinkComponent& component) {

	out["prefabAsset"] = component.prefabAsset ? ToString(component.prefabAsset) : "";
	out["prefabLocalFileID"] = component.prefabLocalFileID ? ToString(component.prefabLocalFileID) : "";
	out["prefabInstanceID"] = component.prefabInstanceID ? ToString(component.prefabInstanceID) : "";
	out["isPrefabRoot"] = component.isPrefabRoot;
}