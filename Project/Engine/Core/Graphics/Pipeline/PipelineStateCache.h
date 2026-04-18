#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsPlatform.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>

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
		// バリアントの種類
		PipelineVariantKind resolvedKind = PipelineVariantKind::GraphicsVertex;

		// フォーマットのハッシュ値
		uint64_t formatHash = 0;

		// メッシュシェーダーが有効か
		bool meshEnabled = false;
		// レイトレーシングが有効か
		bool inlineRayTracingEnabled = false;
		bool dispatchRaysEnabled = false;

		// 比較演算子
		bool operator==(const PipelineCacheKey& rhs) const noexcept;
	};

	//============================================================================
	//	PipelineStateCache class
	//	パイプラインステートキャッシュクラス
	//============================================================================
	class PipelineStateCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PipelineStateCache() = default;
		~PipelineStateCache() = default;

		// パイプラインステートの取得または生成
		const PipelineState* GetORCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID, PipelineVariantKind desiredKind,
			std::span<const DXGI_FORMAT> runtimeRTVFormats, DXGI_FORMAT runtimeDSVFormat);

		// データクリア
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// ハッシュ関数の定義
		struct PipelineCacheKeyHash {
			size_t operator()(const PipelineCacheKey& key) const noexcept {
				size_t h = std::hash<AssetID>{}(key.pipelineAsset);
				h ^= (std::hash<uint32_t>{}(static_cast<uint32_t>(key.resolvedKind)) << 1);
				h ^= (std::hash<uint64_t>{}(key.formatHash) << 2);
				h ^= (std::hash<bool>{}(key.meshEnabled) << 3);
				h ^= (std::hash<bool>{}(key.inlineRayTracingEnabled) << 4);
				h ^= (std::hash<bool>{}(key.dispatchRaysEnabled) << 5);
				return h;
			}
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<PipelineCacheKey, std::unique_ptr<PipelineState>, PipelineCacheKeyHash> cache_;

		//--------- functions ----------------------------------------------------

		// フォーマットのハッシュ値を計算する
		uint64_t HashFormats(std::span<const DXGI_FORMAT> rtvFormats, DXGI_FORMAT dsvFormat);
	};
} // Engine