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
#include <Engine/Core/Rendering/Assets/RenderTextureAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderPassesAsset.h>

// c++
#include <unordered_map>
#include <filesystem>
#include <memory>
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
		const RenderTextureAsset* LoadRenderTexture(AssetID assetID);
		const RenderPassesAsset* LoadRenderPasses(AssetID assetID);
		// Library内の派生Shaderを実行時キャッシュへ登録
		void RegisterDerivedShader(ShaderAsset shader);
		void RegisterDerivedPipeline(RenderPipelineAsset pipeline);
		// プレビュー用の派生Materialを実行時キャッシュへ登録
		void RegisterDerivedMaterial(MaterialAsset material);
		// Render Passesの未保存プレビューを登録する
		void RegisterPreviewRenderPasses(RenderPassesAsset extension);
		// 編集用プレビューを解除してファイル内容へ戻す
		void DiscardPreviewRenderPasses(AssetID assetID);

		// マテリアルのキャッシュを破棄して次回ロードでファイルから読み直させる、実行中の編集反映に使う
		void InvalidateMaterial(AssetID assetID);
		// シェーダーとパイプラインのアセットキャッシュを破棄する
		void InvalidateShader(AssetID assetID) { shaderCache_.erase(assetID); }
		void InvalidatePipeline(AssetID assetID) { pipelineCache_.erase(assetID); }
		// フォントのキャッシュを破棄して次回ロードで内容リビジョンを進める
		void InvalidateFont(AssetID assetID) { fontCache_.erase(assetID); }
		// パーティクルエフェクトのキャッシュを破棄する、実行中の編集反映に使う
		void InvalidateParticleEffect(AssetID assetID) { particleEffectCache_.erase(assetID); }
		void InvalidateRenderTexture(AssetID assetID) { renderTextureCache_.erase(assetID); }
		void InvalidateRenderPasses(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		AssetDatabase* GetDatabase() const { return database_; }
		// 別Libraryと同じ更新番号でも内容を混同しない
		const std::shared_ptr<const uint64_t>& GetMaterialRevision() const { return materialRevision_; }
		uint64_t GetRenderPassesRevision() const { return renderPassesRevision_; }
		uint64_t GetRenderPassesRevision(AssetID assetID) const;
		bool HasPreviewRenderPasses(AssetID assetID) const { return renderPassesPreviews_.contains(assetID); }
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
		// フォントレイアウトキャッシュの世代判定に使うリビジョン
		uint64_t nextFontContentRevision_ = 1;
		uint64_t renderPassesRevision_ = 1;
		uint64_t renderPassesResetRevision_ = 1;
		std::unordered_map<AssetID, uint64_t> renderPassesAssetRevisions_{};
		std::shared_ptr<const uint64_t> materialRevision_ = std::make_shared<const uint64_t>(1);

		// アセットIDからデータへのマップ
		std::unordered_map<AssetID, ShaderAsset> shaderCache_;
		std::unordered_map<AssetID, RenderPipelineAsset> pipelineCache_;
		std::unordered_map<AssetID, MaterialAsset> materialCache_;
		std::unordered_map<AssetID, MSDFFontAsset> fontCache_;
		std::unordered_map<AssetID, ParticleEffectAsset> particleEffectCache_;
		std::unordered_map<AssetID, RenderTextureAsset> renderTextureCache_;
		std::unordered_map<AssetID, RenderPassesAsset> renderPassesCache_;
		std::unordered_map<AssetID, RenderPassesAsset> renderPassesPreviews_;
	};
} // Engine
