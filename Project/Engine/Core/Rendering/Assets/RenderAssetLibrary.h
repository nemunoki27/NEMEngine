#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/MSDFFontAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>

// c++
#include <unordered_map>
#include <filesystem>
#include <cctype>

namespace Engine {

	//============================================================================
	//	RenderAssetLibrary class
	//	描画系アセットをロード、キャッシュするクラス
	//============================================================================
	class RenderAssetLibrary {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenderAssetLibrary() = default;
		~RenderAssetLibrary() = default;

		// 初期化
		void Init(AssetDatabase* database);

		// データクリア
		void Clear();

		// アセットIDからデータをロード
		const ShaderAsset* LoadShader(AssetID assetID);
		const RenderPipelineAsset* LoadPipeline(AssetID assetID);
		const MaterialAsset* LoadMaterial(AssetID assetID);
		const MSDFFontAsset* LoadFont(AssetID assetID);
		const ParticleEffectAsset* LoadParticleEffect(AssetID assetID);
		const RenderFeatureProfileAsset* LoadRenderFeatureProfile(
			AssetID assetID);
		// Library内の派生Shaderを実行時キャッシュへ登録
		void RegisterDerivedShader(ShaderAsset shader);
		void RegisterDerivedPipeline(RenderPipelineAsset pipeline);
		// プレビュー用の派生Materialを実行時キャッシュへ登録
		void RegisterDerivedMaterial(MaterialAsset material);

		// マテリアルのキャッシュを破棄して次回ロードでファイルから読み直させる、実行中の編集反映に使う
		void InvalidateMaterial(AssetID assetID) { materialCache_.erase(assetID); }
		// シェーダーとパイプラインのアセットキャッシュを破棄する
		void InvalidateShader(AssetID assetID) { shaderCache_.erase(assetID); }
		void InvalidatePipeline(AssetID assetID) { pipelineCache_.erase(assetID); }
		// パーティクルエフェクトのキャッシュを破棄する、実行中の編集反映に使う
		void InvalidateParticleEffect(AssetID assetID) { particleEffectCache_.erase(assetID); }
		void InvalidateRenderFeatureProfile(AssetID assetID) {
			renderFeatureProfileCache_.erase(assetID);
		}

		//--------- accessor -----------------------------------------------------

		AssetDatabase* GetDatabase() const { return database_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// IDからJSONアセットを読み込みキャッシュへ格納する共通処理
		template <typename T>
		const T* LoadCachedAsset(std::unordered_map<AssetID, T>& cache, AssetID assetID);
		// 読み込んだアセットの実行時参照を解決
		template <typename T>
		void ResolveRuntimeReferences(T&) {}
		// シェーダーソース参照を実体パスへ解決
		void ResolveRuntimeReferences(ShaderAsset& asset);
		// Shader Graph参照を派生Shaderへ解決
		void ResolveRuntimeReferences(MaterialAsset& asset);

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;

		// アセットIDからデータへのマップ
		std::unordered_map<AssetID, ShaderAsset> shaderCache_;
		std::unordered_map<AssetID, RenderPipelineAsset> pipelineCache_;
		std::unordered_map<AssetID, MaterialAsset> materialCache_;
		std::unordered_map<AssetID, MSDFFontAsset> fontCache_;
		std::unordered_map<AssetID, ParticleEffectAsset> particleEffectCache_;
		std::unordered_map<AssetID, RenderFeatureProfileAsset>
			renderFeatureProfileCache_;
	};
} // Engine
