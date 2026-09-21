#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackendTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	MeshRenderBackend structures
	//============================================================================
	// スキニング頂点データのGPUリソース
	struct SkinnedVertexSource {

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		uint32_t srvIndex = UINT32_MAX;
		uint32_t vertexOffset = 0;
		uint64_t poseGeneration = 0;
		uint64_t bufferGeneration = 0;
	};

	//============================================================================
	//	MeshRenderBackend class
	//	メッシュ描画を処理するクラス
	//============================================================================
	class MeshRenderBackend :
		public IRenderBackend {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshRenderBackend();
		~MeshRenderBackend() override;

		// 可視メッシュのGPUアップロード
		void RequestMeshes(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets);
		// 指定メッシュがすべてGPUへ反映済みか
		bool AreMeshesReady(std::span<const AssetID> meshAssets) const;
		// 全メッシュのGPUリソースを同期作成する
		void PreloadMeshes(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets);
		// 外部編集されたメッシュを再インポートしてバッチキャッシュを無効化する、ホットリロード用
		void RequestMeshReload(AssetID meshAssetID);
		// スキンメッシュのバッチ描画の前処理
		void PreDispatchSkinningBatch(const RenderDrawContext& context,
			std::span<const RenderItem* const> items);

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		// スキニング頂点のGPUリソースを検索する
		bool FindSkinnedVertexSource(ECSWorld* world, Entity entity, AssetID mesh, SkinnedVertexSource& outSource) const;

		const MeshGPUResource* FindMeshResource(AssetID meshAssetID) const { return meshResourceManager_.Find(meshAssetID); }
		uint64_t GetMeshResourceRevision() const { return meshResourceManager_.GetResourceRevision(); }

		// Worldに紐づくバッチキャッシュを即時クリアする
		void ClearWorldBatchCaches();

		uint32_t GetID() const override { return RenderBackendID::Mesh; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// スキンメッシュのバッチキャッシュのキー
		struct SkinnedBatchCacheKey {

			ECSWorld* world = nullptr;
			AssetID mesh{};
			uint64_t hash = 0;

			bool operator==(const SkinnedBatchCacheKey& rhs) const noexcept;
		};
		// ハッシュ関数
		struct SkinnedBatchCacheKeyHash {
			size_t operator()(const SkinnedBatchCacheKey& key) const noexcept;
		};
		struct SkinnedBatchCacheEntry {

			std::unique_ptr<MeshBatchResources> resources{};
			uint64_t lastUsedFrame = 0;
			uint64_t lastUploadFrame = 0;
		};
		// スキニング頂点のGPUリソースを検索するためのキー
		struct SkinnedSourceLookupKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID mesh{};

			bool operator==(const SkinnedSourceLookupKey& rhs) const noexcept;
		};
		struct SkinnedSourceLookupKeyHash {
			size_t operator()(const SkinnedSourceLookupKey& key) const noexcept;
		};
	struct StaticBatchCacheKey {

		// 静的メッシュはWorld/Asset/内容Hashが一致すればGPUバッファを再利用する
		ECSWorld* world = nullptr;
		AssetID mesh{};
		uint64_t hash = 0;

			bool operator==(const StaticBatchCacheKey& rhs) const noexcept;
		};
		struct StaticBatchCacheKeyHash {
			size_t operator()(const StaticBatchCacheKey& key) const noexcept;
		};
	struct StaticBatchCacheEntry {

		// 静的バッチ用に保持し続けるMeshBatchResources
		std::unique_ptr<MeshBatchResources> resources{};
		// 一定フレーム使われなければ破棄する
		uint64_t lastUsedFrame = 0;
		// CPU側描画データを構築したRender世代
		uint64_t renderRevision = 0;
		// CPU側インスタンス行列を構築したTransform世代
		uint64_t transformRevision = 0;
		// FallbackTextureを含む場合は後で本テクスチャに差し替わるため永続化しない
		bool persistent = false;
	};

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<MeshBatchResources> resourcePool_{};

		// メッシュのGPUリソース管理クラス
		MeshGPUResourceManager meshResourceManager_{};

		// バッファレジストリ→ Graphicsパイプラインスロットの対応キャッシュ
		RegistryAutoBindTable registryAutoBindTable_{};
		// メッシュ固有GraphicsバインドのパイプラインスロットID
		PipelineBindingCache sharedBindCache_{};
		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID drawCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID packedVtxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID vtxSubMeshSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedVtxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedPkdVtxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID subMeshSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID occlusionDepthSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outlineSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID screenSpaceOutlineMaskCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID shaderGraphTimeCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		// reflection駆動のマテリアルパラメータcbuffer、カスタムマテリアル用でBuiltinには存在しない
		PipelineBindingCache::SlotID materialParamsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		MaterialParameterBinder materialParamBinder_{};
		PostProcessConstantBufferAllocator constantBufferAllocator_{};
		D3D12_GPU_VIRTUAL_ADDRESS shaderGraphTimeGPUAddress_ = 0;
		// reflection駆動のサブメッシュ単位マテリアルパラメータ構造化バッファのスロット
		PipelineBindingCache::SlotID subMeshMaterialParamSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		// スキニングComputeバインドのパイプラインスロットID
		PipelineBindingCache skinningBindCache_{};
		PipelineBindingCache::SlotID skinConstCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID inputVtxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID vtxInflSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinPaletteSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedVtxUAVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedPkdVtxUAVSlot_ = PipelineBindingCache::kInvalidSlot;

		// 描画パス
		std::vector<std::unique_ptr<IMeshDrawPath>> drawPaths_{};

		// スキニング処理に使用するパイプライン
		AssetID skinningPipeline_{};
		// スキンメッシュのバッチキャッシュ
		std::unordered_map<SkinnedBatchCacheKey,
			SkinnedBatchCacheEntry, SkinnedBatchCacheKeyHash> skinnedBatchCache_{};
		std::unordered_map<SkinnedSourceLookupKey, SkinnedVertexSource, SkinnedSourceLookupKeyHash> skinnedSourceLookup_{};
		// カメラ移動時に静的メッシュのCPUアップロードを繰り返さないためのキャッシュ
		std::unordered_map<StaticBatchCacheKey, StaticBatchCacheEntry, StaticBatchCacheKeyHash> staticBatchCache_{};
		// 静的キャッシュの寿命管理用フレーム番号
		uint64_t frameIndex_ = 0;

		// 初期化状態
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 初期化されていない場合は初期化する
		void EnsureInitialized(GraphicsCore& graphicsCore);

		bool PrepareBatchResources(const RenderDrawContext& context,
			std::span<const RenderItem* const> items, MeshPreparedBatch& outPrepared);
		bool PrepareBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			MeshPreparedBatch& outPrepared);
		void BindSharedResources(const RenderDrawContext& context, const MeshPreparedBatch& prepared,
			ID3D12GraphicsCommandList6* commandList);
		IMeshDrawPath& SelectDrawPath(const PipelineVariantDesc& variant);

		// スキンメッシュのバッチキャッシュを構築するためのハッシュを計算する
		uint64_t BuildBatchHash(std::span<const RenderItem* const> items) const;
		uint64_t BuildStaticBatchHash(
			std::span<const RenderItem* const> items,
			const MeshGPUResource& gpuMesh) const;
		// 長時間使われていない静的バッチを破棄する
		void PruneStaticBatchCache();
		// 長時間使われていないスキニングバッチを破棄する
		void PruneSkinnedBatchCache();
		void ClearSkinnedBatchCache();
		// スキンメッシュのGPUディスパッチ
		void DispatchSkinning(const RenderDrawContext& context, const MeshPreparedBatch& prepared);

		// スキンメッシュのリソースをキャッシュに登録する
		void RegisterSkinnedSources(AssetID meshAssetID, MeshBatchResources& resources,
			std::span<const RenderItem* const> items);
	};
} // Engine

