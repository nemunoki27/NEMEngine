#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabPromotion {

	// 追加実体をPrefabへ昇格できるか判定する
	bool CanPromoteAddedEntitySubtree(ECSWorld& world, const Entity& root, UUID instanceID);

	// 追加実体をPrefab文書へ反映する
	bool PromoteAddedEntitySubtrees(nlohmann::json& prefabFileJson, ECSWorld& world,
		AssetID prefabAsset, UUID instanceID, const std::vector<Entity>& roots);
}
