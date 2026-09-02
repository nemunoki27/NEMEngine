#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

// c++
#include <memory>
#include <span>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PipelineStateCache structure
	//============================================================================
	/// パイプラインステートキャッシュのキー構造体
	struct PipelineCacheKey {

		// パイプラインアセットID
		AssetID pipelineAsset{};
		// Rendererが提供する形状パイプライン
		AssetID geometryPipelineAsset{};
		// 状態パイプラインが参照するシェーダー
		AssetID pipelineShaderAsset{};
		// 形状パイプラインが参照するシェーダー
		AssetID geometryShaderAsset{};
		// Materialが提供する部分シェーダー
		AssetID shaderOverrideAsset{};
		// バリアントの種類
		PipelineVariantKind resolvedKind = PipelineVariantKind::GraphicsVertex;

		// フォーマットのハッシュ値
		uint64_t formatHash = 0;

		// メッシュシェーダーが有効か
		bool meshEnabled = false;
		// レイトレーシングが有効か
		bool inlineRayTracingEnabled = false;
		bool dispatchRaysEnabled = false;
		// 深度テスト+書き込みを強制した派生PSOか、3Dテキストなど次元で深度挙動を変える用途で別エントリにする
		bool depthForcedTestWrite = false;
		// 静的サンプラー上書きのハッシュ値
		uint64_t samplerHash = 0;

		// 比較演算子
		bool operator==(const PipelineCacheKey& rhs) const noexcept;
	};

	//============================================================================
	//	PipelineStateCache class
	//	パイプラインステートキャッシュクラス
	//============================================================================
	class PipelineStateCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PipelineStateCache() = default;
		~PipelineStateCache() = default;

		// パイプラインステートの取得または生成
		const PipelineState* GetORCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
			std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat);
		// outVariantを渡すと内部で解決したバリアントを受け取れ、呼び出し側の二重解決を避けられる
		const PipelineState* GetORCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
			std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
			const GraphicsRuntimeFeatures& runtimeFeatures,
			const PipelineVariantDesc** outVariant = nullptr, bool forceDepthTestWrite = false,
			const PipelineStaticSamplerOverrideSet* samplerOverrides = nullptr,
			AssetID shaderOverrideAssetID = {});
		// 状態、形状ステージ、Materialステージを合成して取得する
		const PipelineState* GetORCreateComposed(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, AssetID geometryPipelineAssetID,
			AssetID shaderOverrideAssetID, PipelineVariantKind desiredKind,
			std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat,
			const GraphicsRuntimeFeatures& runtimeFeatures,
			const PipelineVariantDesc** outVariant = nullptr);

		// データクリア
		void Clear();
		// 指定パイプラインアセットIDに一致するエントリを削除する
		void InvalidateByPipelineAsset(AssetID pipelineAssetID);
		// 指定シェーダーを使うPSOを退避し、再生成失敗時に旧PSOへ戻せるようにする
		void InvalidateByShaderAsset(AssetID shaderAssetID);

		// 既に構築済みのグラフィックスパイプラインの統合reflectionを引く、未構築ならnullptr
		// エディタのマテリアルインスペクタがPSOを再生成せずパラメータ一覧を得るために使う
		const ShaderReflectionInfo* FindGraphicsReflection(AssetID pipelineAssetID) const;
		// 部分シェーダーを含む構築済みパイプラインのreflectionを引く
		const ShaderReflectionInfo* FindGraphicsReflection(AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// ハッシュ関数の定義
		struct PipelineCacheKeyHash {
			size_t operator()(const PipelineCacheKey& key) const noexcept {
				size_t h = std::hash<AssetID>{}(key.pipelineAsset);
				h ^= (std::hash<AssetID>{}(key.geometryPipelineAsset) << 1);
				h ^= (std::hash<AssetID>{}(key.pipelineShaderAsset) << 2);
				h ^= (std::hash<AssetID>{}(key.geometryShaderAsset) << 3);
				h ^= (std::hash<AssetID>{}(key.shaderOverrideAsset) << 4);
				h ^= (std::hash<uint32_t>{}(static_cast<uint32_t>(key.resolvedKind)) << 5);
				h ^= (std::hash<uint64_t>{}(key.formatHash) << 6);
				h ^= (std::hash<bool>{}(key.meshEnabled) << 7);
				h ^= (std::hash<bool>{}(key.inlineRayTracingEnabled) << 8);
				h ^= (std::hash<bool>{}(key.dispatchRaysEnabled) << 9);
				h ^= (std::hash<bool>{}(key.depthForcedTestWrite) << 10);
				h ^= (std::hash<uint64_t>{}(key.samplerHash) << 11);
				return h;
			}
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<PipelineCacheKey, std::unique_ptr<PipelineState>, PipelineCacheKeyHash> cache_;
		std::unordered_map<PipelineCacheKey, std::unique_ptr<PipelineState>, PipelineCacheKeyHash> fallbackCache_;
		// pipelineAsset別の統合reflection、エディタからPSO再生成なしで参照するために保持する
		std::unordered_map<AssetID, ShaderReflectionInfo> graphicsReflectionByPipeline_;

		//--------- functions ----------------------------------------------------

		// フォーマットのハッシュ値を計算する
		uint64_t HashFormats(std::span<const DXGI_FORMAT> rtvFormats, DXGI_FORMAT dsvFormat);
		// 静的サンプラー上書きのハッシュ値を計算する
		uint64_t HashStaticSamplerOverrides(const PipelineStaticSamplerOverrideSet* samplerOverrides);
	};
} // Engine

