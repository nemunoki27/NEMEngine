#include "RuntimeAssetPreloadPlan.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <unordered_set>

Engine::RuntimeAssetPreloadPlan Engine::RuntimeAssetPreloadPlan::Collect(const AssetDatabase& database,
	std::span<const AssetID> roots) {

	RuntimeAssetPreloadPlan plan;
	std::vector<AssetID> pending(roots.begin(), roots.end());
	std::unordered_set<AssetID> visited;
	for (size_t i = 0; i < pending.size(); ++i) {
		AssetID id = pending[i];
		if (!id || !visited.insert(id).second) { continue; }
		const AssetMeta* meta = database.Find(id);
		if (!meta) { plan.missing.push_back(id); continue; }
		plan.assets.push_back(id);
		// 起動Scene以外の独立したSceneは列挙しない
		pending.insert(pending.end(), meta->dependencies.begin(), meta->dependencies.end());
	}
	std::sort(plan.assets.begin(), plan.assets.end(), [&](AssetID lhs, AssetID rhs) {
		return database.Find(lhs)->assetPath < database.Find(rhs)->assetPath;
	});
	return plan;
}
