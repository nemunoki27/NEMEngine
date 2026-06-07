#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PostProcessStackRuntime structures
	//	ランタイム実行用にフラット化されたPostProcessStackの情報
	//============================================================================
	// 1パスのランタイム実行データ
	struct PostProcessStackRuntimePass {

		UUID id{};
		std::string name;
		bool enabled = true;
		AssetID material{};
		MaterialPassKind passKind = MaterialPassKind::PostProcess;
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides;
		std::unordered_map<std::string, AssetID> textureGuids;
	};

	// ランタイム実行用のPostProcessStack全体データ
	struct PostProcessStackRuntime {

		std::vector<PostProcessStackRuntimePass> passes;

		// 有効なパスが1つ以上あるか
		bool HasEnabledPasses() const;
	};
} // Engine
