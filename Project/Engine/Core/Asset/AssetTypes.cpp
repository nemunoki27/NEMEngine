#include "AssetTypes.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>

//============================================================================
//	AssetTypes classMethods
//============================================================================

Engine::AssetID Engine::ParseAssetID(const nlohmann::json& in, const char* key) {

	const std::string value = in.value(key, "");
	return value.empty() ? Engine::AssetID{} : Engine::FromString16Hex(value);
}

Engine::AssetID Engine::ParseAssetReference(const nlohmann::json& in, const char* key,
	AssetDatabase* database, AssetType expectedType) {

	const std::string value = in.value(key, "");
	if (value.empty()) {
		return AssetID{};
	}
	// まず UUID として解釈
	{
		AssetID id = FromString16Hex(value);
		if (id) {
			return id;
		}
	}
	// Assets/... パスとして解釈
	if (database) {
		if (const AssetMeta* meta = database->FindByPath(value)) {
			if (expectedType == AssetType::Unknown || meta->type == expectedType) {
				return meta->guid;
			}
		}
	}
	return AssetID{};
}