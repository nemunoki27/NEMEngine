#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabBaseDocument {

	// Prefab文書から比較用の基準データを読み取る
	std::unordered_map<UUID, PrefabBaseEntity> LoadPrefabBaseEntities(AssetDatabase& database,
		AssetID prefabAsset, UUID* outRootLocalFileID = nullptr);
}
