#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <unordered_map>

namespace Engine::AssetDependencyScanner {

	using IDReferences = std::unordered_multimap<AssetID, AssetType>;
	using PathReferences = std::unordered_multimap<std::string, AssetType>;

	// JSONから参照候補と期待型を収集する
	void ScanReferences(const nlohmann::json& node,
		IDReferences& outIDs, PathReferences& outPaths,
		bool includeUnclassified = false);
}
