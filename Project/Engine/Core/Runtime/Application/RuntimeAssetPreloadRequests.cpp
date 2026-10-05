#include "RuntimeAssetPreloadRequests.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <utility>

void Engine::RuntimeAssetPreloadRequests::Add(AssetID asset) {

	// 同一frameの重複要求を省く
	if (asset && std::find(pending_.begin(), pending_.end(), asset) == pending_.end()) {
		pending_.push_back(asset);
	}
}

std::vector<Engine::AssetID> Engine::RuntimeAssetPreloadRequests::Take() {

	return std::exchange(pending_, {});
}
