#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	class AssetDatabase;

	// 必要なAssetの推移依存だけを先読み対象にする
	struct RuntimeAssetPreloadPlan {
		std::vector<AssetID> assets;
		std::vector<AssetID> missing;

		// 起点と依存先を循環・重複なしで収集する
		static RuntimeAssetPreloadPlan Collect(const AssetDatabase& database, std::span<const AssetID> roots);
	};
}
