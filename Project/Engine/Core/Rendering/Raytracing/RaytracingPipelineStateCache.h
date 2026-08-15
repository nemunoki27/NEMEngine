#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>

// c++
#include <array>
#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	RaytracingPipelineCacheKey structure
	//============================================================================
	struct RaytracingPipelineCacheKey {

		AssetID pipelineAsset{};
		AssetID pipelineShaderAsset{};
		AssetID shaderOverrideAsset{};

		bool operator==(const RaytracingPipelineCacheKey& rhs) const noexcept;
	};

	//============================================================================
	//	RaytracingPipelineStateCache class
	//	レイトレーシングパイプラインステートのキャッシュを管理するクラス
	//============================================================================
	class RaytracingPipelineStateCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RaytracingPipelineStateCache() = default;
		~RaytracingPipelineStateCache() = default;

		// パイプラインステートの取得、キャッシュに存在しない場合は作成してキャッシュする
		RaytracingPipelineState* GetOrCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID = {});

		// データクリア
		void Clear();
		// 指定Pipelineから生成したState Objectだけを破棄する
		void InvalidateByPipelineAsset(AssetID pipelineAssetID);
		// 指定Shaderから生成したState Objectだけを破棄する
		void InvalidateByShaderAsset(AssetID shaderAssetID);

		//--------- accessor -----------------------------------------------------

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		struct RaytracingPipelineCacheKeyHash {
			size_t operator()(const RaytracingPipelineCacheKey& key) const noexcept {

				size_t hash = std::hash<AssetID>{}(key.pipelineAsset);
				hash ^= std::hash<AssetID>{}(key.pipelineShaderAsset) << 1;
				hash ^= std::hash<AssetID>{}(key.shaderOverrideAsset) << 2;
				return hash;
			}
		};
		struct PendingBuild {

			std::future<std::unique_ptr<RaytracingPipelineState>> result;
			uint64_t revision = 0;
		};

		std::unordered_map<RaytracingPipelineCacheKey,
			std::unique_ptr<RaytracingPipelineState>,
			RaytracingPipelineCacheKeyHash> cache_{};
		std::unordered_map<RaytracingPipelineCacheKey,
			std::unique_ptr<RaytracingPipelineState>,
			RaytracingPipelineCacheKeyHash> fallbackCache_{};
		std::unordered_map<RaytracingPipelineCacheKey, PendingBuild,
			RaytracingPipelineCacheKeyHash> pendingBuilds_{};
		std::unordered_map<RaytracingPipelineCacheKey, uint64_t,
			RaytracingPipelineCacheKeyHash> revisions_{};
		std::unordered_map<RaytracingPipelineCacheKey, uint64_t,
			RaytracingPipelineCacheKeyHash> failedRevisions_{};
		std::array<std::vector<std::unique_ptr<RaytracingPipelineState>>,
			kGraphicsFrameContextCount> retiredStates_{};
		std::array<uint64_t, kGraphicsFrameContextCount>
			retiredFrameSerials_{};

		// 現在フレームスロットをGPUが再利用できる時点で旧State Objectを解放する
		void CollectRetiredStates();
		// 現在フレームで使用された可能性があるState Objectを遅延解放へ移す
		void RetireState(std::unique_ptr<RaytracingPipelineState> state);
		// 同じマテリアル要求に対応する直前の有効なState Objectを探す
		RaytracingPipelineState* FindFallback(AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID) const;
		// 指定キーの旧State Objectをフォールバックへ退避する
		void PreserveFallback(const RaytracingPipelineCacheKey& key,
			std::unique_ptr<RaytracingPipelineState> state);
		// 同じマテリアル要求に属する旧State Objectを遅延解放へ移す
		void RetireFallbacks(AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID);
		// 旧State Objectを表示したまま更新版をバックグラウンド生成する
		RaytracingPipelineState* UpdateAsyncBuild(
			ID3D12Device8* device,
			const RaytracingPipelineCacheKey& key,
			const PipelineVariantDesc& variant,
			const ShaderAsset& shaderAsset);
	};
} // Engine

