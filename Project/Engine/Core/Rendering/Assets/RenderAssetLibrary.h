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
#include <unordered_set>
#include <filesystem>
#include <memory>
#include <cctype>

namespace Engine {

	struct FontSourceSnapshot;

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
		// FontとAtlasの固定した読込世代を取得する
		std::shared_ptr<const FontSourceSnapshot> LoadFontSource(AssetID assetID);
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
		// 次回の読込を予約し、失敗時は旧Fontを保持する
		void InvalidateFont(AssetID assetID);
		// パーティクルエフェクトのキャッシュを破棄する、実行中の編集反映に使う
		void InvalidateParticleEffect(AssetID assetID) { particleEffectCache_.erase(assetID); }
		void InvalidateRenderTexture(AssetID assetID) { renderTextureCache_.erase(assetID); }
		void InvalidateRenderPasses(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		AssetDatabase* GetDatabase() const { return database_; }
		// 終了とClearを跨いだFont描画の借用を区別する
		const std::shared_ptr<const uint64_t>& GetFontCacheIdentity() const { return fontCacheIdentity_; }
		// 別Libraryと同じ更新番号でも内容を混同しない
		const std::shared_ptr<const uint64_t>& GetMaterialRevision() const { return materialRevision_; }
		uint64_t GetRenderPassesRevision() const { return renderPassesRevision_; }
		uint64_t GetRenderPassesRevision(AssetID assetID) const;
		bool HasPreviewRenderPasses(AssetID assetID) const { return renderPassesPreviews_.contains(assetID); }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;
		// フォントレイアウトキャッシュの世代判定に使うリビジョン
		uint64_t nextFontContentRevision_ = 1;
		uint64_t renderPassesRevision_ = 1;
		uint64_t renderPassesResetRevision_ = 1;
		std::unordered_map<AssetID, uint64_t> renderPassesAssetRevisions_{};
		std::shared_ptr<const uint64_t> materialRevision_ = std::make_shared<const uint64_t>(1);
		std::shared_ptr<const uint64_t> fontCacheIdentity_ = std::make_shared<const uint64_t>(1);

		// アセットIDからデータへのマップ
		std::unordered_map<AssetID, ShaderAsset> shaderCache_;
		std::unordered_map<AssetID, RenderPipelineAsset> pipelineCache_;
		std::unordered_map<AssetID, MaterialAsset> materialCache_;
		std::unordered_map<AssetID, std::shared_ptr<const FontSourceSnapshot>> fontCache_;
		std::unordered_set<AssetID> invalidatedFonts_;
		std::unordered_map<AssetID, ParticleEffectAsset> particleEffectCache_;
		std::unordered_map<AssetID, RenderTextureAsset> renderTextureCache_;
		std::unordered_map<AssetID, RenderPassesAsset> renderPassesCache_;
		std::unordered_map<AssetID, RenderPassesAsset> renderPassesPreviews_;

		//--------- functions ----------------------------------------------------

		// IDからJSONアセットを読み込みキャッシュへ格納する
		template <typename T> const T* LoadCachedAsset(std::unordered_map<AssetID, T>& cache, AssetID assetID);
		// 読み込んだアセットの実行時参照を解決する
		template <typename T> void ResolveRuntimeReferences(T&) {}
		// シェーダーソース参照を実体パスへ解決する
		void ResolveRuntimeReferences(ShaderAsset& asset);
		// Shader Graph参照を派生Shaderへ解決する
		void ResolveRuntimeReferences(MaterialAsset& asset);
	};
} // namespace Engine
