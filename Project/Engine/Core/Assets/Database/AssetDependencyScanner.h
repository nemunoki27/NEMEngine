#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <unordered_map>

namespace Engine::AssetDependencyScanner {

	// JSONから参照候補と期待型を収集する
	void ScanReferences(const nlohmann::json& node,
		std::unordered_map<AssetID, AssetType>& outIDs, std::unordered_map<std::string, AssetType>& outPaths);
}
