#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabPropagation {

	// 差分を保持して伝播し失敗時は退避状態へ戻す
	bool PropagateToInstances(ECSWorld& world, AssetDatabase& database, HierarchySystem& hierarchySystem,
		AssetID prefabAsset, const std::unordered_map<UUID, PrefabBaseEntity>& oldBase);
}
