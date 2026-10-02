#include "RenderExtensionAsset.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileSerializer.h"

//============================================================================
//	RenderExtensionAsset functions
//============================================================================

bool Engine::FromJson(const nlohmann::json& data, RenderExtensionAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}
	const RenderFeatureProfileAsset source = RenderFeatureProfileSerializer::FromJson(data);
	RenderExtensionAsset loaded{};
	loaded.guid = ParseAssetID(data, "guid");
	loaded.name = source.name;
	loaded.passes = source.passes;
	loaded.hierarchy = source.hierarchy;
	outAsset = std::move(loaded);
	return true;
}

nlohmann::json Engine::ToJson(const RenderExtensionAsset& asset) {

	nlohmann::json data = RenderFeatureProfileSerializer::ToJson(ToRuntimeProfile(asset));
	data.erase("colorPipeline");
	data["version"] = 1u;
	if (asset.guid) {
		data["guid"] = ToString(asset.guid);
	}
	return data;
}

Engine::RenderFeatureProfileAsset Engine::ToRuntimeProfile(const RenderExtensionAsset& asset) {

	RenderFeatureProfileAsset profile{};
	profile.guid = asset.guid;
	profile.name = asset.name;
	profile.passes = asset.passes;
	profile.hierarchy = asset.hierarchy;
	return profile;
}
