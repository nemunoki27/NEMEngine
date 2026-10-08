#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabOverrideCapture {

	// Prefabインスタンスの差分を取得する
	PrefabInstanceData CaptureInstance(ECSWorld& world, AssetDatabase& database, UUID instanceID,
		const std::unordered_map<UUID, PrefabBaseEntity>& base);

	// 指定実体の差分概要を取得する
	EntityOverrideInfo CaptureEntityOverride(ECSWorld& world, const Entity& entity, AssetDatabase& database);
}
