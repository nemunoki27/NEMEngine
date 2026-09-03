#include "PrefabLinkComponent.h"

//============================================================================
//	PrefabLinkComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, PrefabLinkComponent& component) {

	std::string prefabAsset = in.value("prefabAsset", "");
	std::string prefabLocalFileID = in.value("prefabLocalFileID", "");
	std::string prefabInstanceID = in.value("prefabInstanceID", "");
	std::string ownerPrefabInstanceID = in.value("ownerPrefabInstanceID", "");
	std::string nestedSlotID = in.value("nestedSlotID", "");

	component.prefabAsset = prefabAsset.empty() ? AssetID{} : FromString32Hex(prefabAsset);
	component.prefabLocalFileID = prefabLocalFileID.empty() ? UUID{} : FromString16Hex(prefabLocalFileID);
	component.prefabInstanceID = prefabInstanceID.empty() ? UUID{} : FromString16Hex(prefabInstanceID);
	component.ownerPrefabInstanceID = ownerPrefabInstanceID.empty() ? UUID{} : FromString16Hex(ownerPrefabInstanceID);
	component.nestedSlotID = nestedSlotID.empty() ? UUID{} : FromString16Hex(nestedSlotID);
	component.isPrefabAssetNested = in.value("isPrefabAssetNested", false);
	component.isPrefabRoot = in.value("isPrefabRoot", false);
}

void Engine::to_json(nlohmann::json& out, const PrefabLinkComponent& component) {

	out["prefabAsset"] = ToAssetReferenceJson(component.prefabAsset);
	out["prefabLocalFileID"] = component.prefabLocalFileID ? ToString(component.prefabLocalFileID) : "";
	out["prefabInstanceID"] = component.prefabInstanceID ? ToString(component.prefabInstanceID) : "";
	out["ownerPrefabInstanceID"] = component.ownerPrefabInstanceID ? ToString(component.ownerPrefabInstanceID) : "";
	out["nestedSlotID"] = component.nestedSlotID ? ToString(component.nestedSlotID) : "";
	out["isPrefabAssetNested"] = component.isPrefabAssetNested;
	out["isPrefabRoot"] = component.isPrefabRoot;
}
