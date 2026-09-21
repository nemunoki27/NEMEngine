#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabInstanceOwnership {

	// 指定Prefabインスタンスの実体を収集する
	std::vector<Entity> CollectInstanceEntities(ECSWorld& world, UUID instanceID);

	// 階層からネストPrefabの所属を同期する
	void SynchronizeNestedPrefabOwnership(ECSWorld& world);
}
