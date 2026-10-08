#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabGenerationContext.h>

namespace Engine::PrefabInstanceRebuilder {

	// 保存された対応と差分からインスタンスを復元する
	Entity RebuildInstance(ECSWorld& world, AssetDatabase& database, HierarchySystem& hierarchySystem,
		const PrefabInstanceData& data, UUID sceneInstanceID, uint32_t nestedDepth = 0);
		// ネスト生成の処理対象を共有して差分を復元する
	Entity RebuildInstance(PrefabGenerationContext& context, const PrefabInstanceData& data,
		UUID sceneInstanceID, uint32_t nestedDepth);
}
