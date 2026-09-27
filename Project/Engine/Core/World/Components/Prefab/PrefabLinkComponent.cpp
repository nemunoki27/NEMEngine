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
	component.savedInstanceID = FromString16Hex(in.value("savedInstanceID", ""));
	component.ownerPrefabInstanceID = ownerPrefabInstanceID.empty() ? UUID{} : FromString16Hex(ownerPrefabInstanceID);
	component.nestedSlotID = nestedSlotID.empty() ? UUID{} : FromString16Hex(nestedSlotID);
	component.isPrefabAssetNested = in.value("isPrefabAssetNested", false);
	component.isPrefabRoot = in.value("isPrefabRoot", false);
	component.addedEntityMap.clear();
	if (in.contains("addedEntityMap")) {
		for (const auto& entry : in["addedEntityMap"].get_ref<const nlohmann::json::array_t&>()) {
			component.addedEntityMap.emplace_back(FromString16Hex(entry.at("P").get<std::string>()),
				FromString16Hex(entry.at("S").get<std::string>()));
		}
	}
}

void Engine::to_json(nlohmann::json& out, const PrefabLinkComponent& component) {

	out["prefabAsset"] = ToAssetReferenceJson(component.prefabAsset);
	out["prefabLocalFileID"] = component.prefabLocalFileID ? ToString(component.prefabLocalFileID) : "";
	out["prefabInstanceID"] = component.prefabInstanceID ? ToString(component.prefabInstanceID) : "";
	out["savedInstanceID"] = component.savedInstanceID ? ToString(component.savedInstanceID) : "";
	out["ownerPrefabInstanceID"] = component.ownerPrefabInstanceID ? ToString(component.ownerPrefabInstanceID) : "";
	out["nestedSlotID"] = component.nestedSlotID ? ToString(component.nestedSlotID) : "";
	out["isPrefabAssetNested"] = component.isPrefabAssetNested;
	out["isPrefabRoot"] = component.isPrefabRoot;
	out["addedEntityMap"] = nlohmann::json::array();
	for (const auto& [source, target] : component.addedEntityMap) {
		out["addedEntityMap"].push_back({ { "P", ToString(source) }, { "S", ToString(target) } });
	}
}
