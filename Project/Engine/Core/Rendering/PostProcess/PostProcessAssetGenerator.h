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
	// Builtin PostProcess用のshader/pipeline/material JSONを補完するクラス
	//============================================================================
	class PostProcessAssetGenerator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		PostProcessAssetGenerator() = default;
		~PostProcessAssetGenerator() = default;

		// HLSLが存在するBuiltin PostProcessのJSONを作成/更新する
		void EnsureBuiltinAssets(AssetDatabase* database);
		// 状態をリセットする
		void Clear();

		// 指定した .CS.hlsl の論理アセットパスからPostProcessアセット一式を生成または検索してMaterialのAssetIDを返す
		static AssetID EnsureUserAsset(AssetDatabase* database, const std::string& csHlslAssetPath);
		// 指定した .shader.json の論理アセットパスから対応するMaterialのAssetIDを返す。なければ生成する
		static AssetID FindOrCreateMaterialForShader(AssetDatabase* database, const std::string& shaderAssetPath);

		//--------- accessor -----------------------------------------------------

		AssetID FindBuiltinMaterial(std::string_view name) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;

		std::unordered_map<std::string, AssetID> materialTable_{};
		bool generated_ = false;
	};
} // Engine

