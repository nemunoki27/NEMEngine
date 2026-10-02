#include "VolumeProfileAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>

//============================================================================
//	VolumeProfileAsset functions
//============================================================================
bool Engine::FromJson(const nlohmann::json& data, VolumeProfileAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	VolumeProfileAsset loaded{};
	loaded.guid = ParseAssetID(data, "guid");
	loaded.name = data.value("name", loaded.name);
	RenderFeatureProfileAsset compatibility = RenderFeatureProfileSerializer::FromJson(data);
	loaded.colorPipeline = compatibility.colorPipeline;
	outAsset = std::move(loaded);
	return true;
}

nlohmann::json Engine::ToJson(const VolumeProfileAsset& asset) {

	RenderFeatureProfileAsset compatibility{};
	compatibility.guid = asset.guid;
	compatibility.name = asset.name;
	compatibility.colorPipeline = asset.colorPipeline;
	nlohmann::json data = RenderFeatureProfileSerializer::ToJson(compatibility);
	data.erase("passes");
	data.erase("hierarchy");
	data["version"] = 1u;
	if (asset.guid) {
		data["guid"] = ToString(asset.guid);
	}
	return data;
}
