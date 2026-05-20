#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string_view>
#include <unordered_map>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	PostProcessAssetGenerator class
	//	Builtin PostProcess用のshader/pipeline/material JSONを補完するクラス。
	//============================================================================
	class PostProcessAssetGenerator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessAssetGenerator() = default;
		~PostProcessAssetGenerator() = default;

		// HLSLが存在するBuiltin PostProcessのJSONを作成/更新する
		void EnsureBuiltinAssets(AssetDatabase* database);
		// 状態をリセットする
		void Clear();

		//--------- accessor -----------------------------------------------------

		AssetID FindBuiltinMaterial(std::string_view name) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;

		std::unordered_map<std::string, AssetID> materialTable_{};
		bool generated_ = false;
	};
} // Engine
