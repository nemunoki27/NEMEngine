#include "RenderTextureAsset.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	RenderTextureAsset functions
//============================================================================
bool Engine::FromJson(const nlohmann::json& data, RenderTextureAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	RenderTextureAsset loaded{};
	loaded.guid = ParseAssetID(data, "guid");
	loaded.width = std::clamp(data.value("width", loaded.width), 1u, 16384u);
	loaded.height = std::clamp(data.value("height", loaded.height), 1u, 16384u);
	outAsset = loaded;
	return true;
}

nlohmann::json Engine::ToJson(const RenderTextureAsset& asset) {

	return nlohmann::json{
		{ "guid", asset.guid ? ToString(asset.guid) : "" },
		{ "width", asset.width },
		{ "height", asset.height },
	};
}
