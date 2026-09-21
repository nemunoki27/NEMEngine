#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabBaseDocument {

	// Prefab文書から比較用の基準データを読み取る
	std::unordered_map<UUID, PrefabBaseEntity> LoadPrefabBaseEntities(AssetDatabase& database,
		AssetID prefabAsset, UUID* outRootLocalFileID = nullptr);

		// 更新時刻に対応する基準データを取得する
		const std::unordered_map<UUID, PrefabBaseEntity>& LoadPrefabBaseEntitiesCached(
			AssetDatabase& database, AssetID prefabAsset);

	// 保存後の基準データを失効させる
	void InvalidatePrefabBaseCache(AssetID prefabAsset);
}
