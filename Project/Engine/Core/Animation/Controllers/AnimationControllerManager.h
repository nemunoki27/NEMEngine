#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationControllerAsset.h"

// c++
#include <unordered_map>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	AnimationControllerManager class
	//	検証済みControllerの定義と公開世代を所有する
	//============================================================================
	class AnimationControllerManager {
	public:
		struct Definition {

			AnimationControllerAsset asset;
			uint64_t generation = 0;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		const Definition* GetOrLoad(AssetDatabase& database, AssetID assetID);
		void Clear();
	private:
		struct CacheEntry {

			std::optional<Definition> definition;
			std::filesystem::path path;
			uint64_t contentRevision = UINT64_MAX;
			uint64_t structureRevision = UINT64_MAX;
		};

		std::unordered_map<AssetID, CacheEntry> entries_;
		uint64_t nextGeneration_ = 1;
	};
}
