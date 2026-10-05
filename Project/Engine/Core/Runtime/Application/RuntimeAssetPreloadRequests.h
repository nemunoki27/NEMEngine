#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <vector>

namespace Engine {

	// Worldの終了とともに未処理の先読み要求を破棄する
	class RuntimeAssetPreloadRequests {
	public:
		// 同じAssetへの要求をまとめる
		void Add(AssetID asset);
		// 安全地点へ未処理の要求を渡す
		std::vector<AssetID> Take();
	private:
		std::vector<AssetID> pending_;
	};
}
