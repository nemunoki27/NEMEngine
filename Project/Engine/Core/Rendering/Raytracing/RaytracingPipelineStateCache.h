#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingStructures.h>

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
		uint64_t samplerHash = 0;

		bool operator==(const RaytracingPipelineCacheKey& rhs) const noexcept;
	};

	//============================================================================
	//	RaytracingPipelineStateCache class
	//	レイトレーシングパイプラインステートのキャッシュを管理するクラス
	//============================================================================
	enum class RaytracingPipelineReadiness { Ready, Building, Failed };

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
			AssetID shaderOverrideAssetID = {},
			const PipelineStaticSamplerOverrideSet* samplerOverrides = nullptr);

		RaytracingPipelineReadiness PrepareLatest(GraphicsPlatform& platform, RenderAssetLibrary& assets,
			AssetID pipeline, AssetID shader, const PipelineStaticSamplerOverrideSet& samplers);

		// 共有のRayGenへ各Materialの命中Shaderを組み合わせる
		RaytracingPipelineState* GetMaterialReflection(GraphicsPlatform& platform, RenderAssetLibrary& assets,
			const PipelineStaticSamplerOverrideSet& samplers, std::span<const RaytracingHitMaterial> materials,
			bool prepareScene, std::span<const RaytracingHitMaterial>& activeMaterials);

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
			size_t operator()(const RaytracingPipelineCacheKey& key) const noexcept;
		};
		struct PendingBuild {

			std::future<std::unique_ptr<RaytracingPipelineState>> result;
			uint64_t revision = 0;
		};

		struct MaterialReflectionState {
			std::vector<RaytracingHitMaterial> materials;
			uint64_t samplerHash = 0;
			std::vector<RaytracingHitMaterial> activeMaterials;
			std::vector<RaytracingHitMaterial> buildingMaterials;
			std::vector<AssetID> shaderDependencies;
			PendingBuild build;
			uint64_t revision = 1;
			uint64_t requestedRevision = 0;
			uint64_t lastPollFrame = UINT64_MAX;
			bool dirty = false;
			std::unique_ptr<RaytracingPipelineState> state;
		};
		std::vector<MaterialReflectionState> materialReflections_;
		bool StartMaterialReflectionBuild(GraphicsPlatform& platform, RenderAssetLibrary& assets,
			const PipelineStaticSamplerOverrideSet& samplers, MaterialReflectionState& entry);

		GraphicsResourceRetirement* retirement_ = nullptr;
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
		GraphicsFrameResourceArray<std::vector<std::unique_ptr<RaytracingPipelineState>>> retiredStates_{};
		std::array<uint64_t, kGraphicsFrameContextCount>
			retiredFrameSerials_{};

		// 現在フレームスロットをGPUが再利用できる時点で旧State Objectを解放する
		void CollectRetiredStates();
		// 現在フレームで使用された可能性があるState Objectを遅延解放へ移す
		void RetireState(std::unique_ptr<RaytracingPipelineState> state);
		// 同じマテリアル要求に対応する直前の有効なState Objectを探す
		RaytracingPipelineState* FindFallback(AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID, uint64_t samplerHash) const;
		// 指定キーの旧State Objectをフォールバックへ退避する
		void PreserveFallback(const RaytracingPipelineCacheKey& key,
			std::unique_ptr<RaytracingPipelineState> state);
		// 同じマテリアル要求に属する旧State Objectを遅延解放へ移す
		void RetireFallbacks(AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID, uint64_t samplerHash);
		// 旧State Objectを表示したまま更新版をバックグラウンド生成する
		RaytracingPipelineState* UpdateAsyncBuild(
			ID3D12Device8* device,
			const RaytracingPipelineCacheKey& key,
			const PipelineVariantDesc& variant,
			const ShaderAsset& shaderAsset,
			const PipelineStaticSamplerOverrideSet* samplerOverrides);
	};
} // Engine

