#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	PostProcessAssetGenerator class
	//	標準効果の生成と名前による参照を管理する
	//============================================================================
	class PostProcessAssetGenerator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PostProcessAssetGenerator() = default;
		~PostProcessAssetGenerator() = default;

		// 標準HLSLの文書を生成し、成功後に一覧を公開する
		void EnsureBuiltinAssets(AssetDatabase* database);
		// 状態をリセットする
		void Clear();

		// Compute HLSLの文書一式を生成してMaterialを返す
		static AssetID EnsureUserAsset(AssetDatabase* database, const std::string& csHlslAssetPath);
		// Compute Shaderのソースパスか判定する
		static bool IsComputeShaderSourcePath(std::string_view assetPath);
		// 既存Shaderに対応するMaterialを取得または生成する
		static AssetID FindOrCreateMaterialForShader(AssetDatabase* database, const std::string& shaderAssetPath);

		//--------- accessor -----------------------------------------------------

		// 標準効果の識別子を名前で取得する
		AssetID FindBuiltinMaterial(std::string_view name) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;				// 生成済み一覧の索引
		std::weak_ptr<const uint8_t> databaseLifetime_; // 索引の借用寿命
		uint64_t structureRevision_ = 0;				// 一覧を生成した構造世代

		std::unordered_map<std::string, AssetID> materialTable_{}; // 名前とMaterialの対応
		bool generated_ = false;								   // 一覧の生成完了
	};
}
