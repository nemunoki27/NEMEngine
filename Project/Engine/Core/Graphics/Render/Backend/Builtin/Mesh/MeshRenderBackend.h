#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackendTypes.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Pipeline/Bind/ComputeRootBinder.h>

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
	};

	//============================================================================
	//	MeshRenderBackend class
	//	メッシュ描画を処理するクラス
	//============================================================================
	class MeshRenderBackend :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshRenderBackend();
		~MeshRenderBackend() = default;

		// 可視メッシュのGPUアップロード
		void RequestMeshes(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets);
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

		uint32_t GetID() const override { return RenderBackendID::Mesh; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// スキンメッシュのバッチキャッシュのキー
		struct SkinnedBatchCacheKey {

			ECSWorld* world = nullptr;
			AssetID mesh{};
			uint64_t hash = 0;

			bool operator==(const SkinnedBatchCacheKey& rhs) const noexcept {
				return world == rhs.world && mesh == rhs.mesh && hash == rhs.hash;
			}
		};
		// ハッシュ関数
		struct SkinnedBatchCacheKeyHash {
			size_t operator()(const SkinnedBatchCacheKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<AssetID>{}(key.mesh) << 1);
				h ^= (std::hash<uint64_t>{}(key.hash) << 2);
				return h;
			}
		};
		// スキニング頂点のGPUリソースを検索するためのキー
		struct SkinnedSourceLookupKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID mesh{};

			bool operator==(const SkinnedSourceLookupKey& rhs) const noexcept {
				return world == rhs.world && entity.index == rhs.entity.index &&
					entity.generation == rhs.entity.generation && mesh == rhs.mesh;
			}
		};
		struct SkinnedSourceLookupKeyHash {
			size_t operator()(const SkinnedSourceLookupKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
				h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
				h ^= (std::hash<AssetID>{}(key.mesh) << 3);
				return h;
			}
		};
	struct StaticBatchCacheKey {

		// 静的メッシュはWorld/Asset/内容Hashが一致すればGPUバッファを再利用する
		ECSWorld* world = nullptr;
		AssetID mesh{};
		uint64_t hash = 0;

			bool operator==(const StaticBatchCacheKey& rhs) const noexcept {
				return world == rhs.world && mesh == rhs.mesh && hash == rhs.hash;
			}
		};
		struct StaticBatchCacheKeyHash {
			size_t operator()(const StaticBatchCacheKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<AssetID>{}(key.mesh) << 1);
				h ^= (std::hash<uint64_t>{}(key.hash) << 2);
				return h;
			}
		};
	struct StaticBatchCacheEntry {

		// 静的バッチ用に保持し続けるMeshBatchResources
		std::unique_ptr<MeshBatchResources> resources{};
		// 一定フレーム使われなければ破棄する
		uint64_t lastUsedFrame = 0;
		// FallbackTextureを含む場合は後で本テクスチャに差し替わるため永続化しない
		bool persistent = false;
	};

		//--------- variables ----------------------------------------------------

		// バッチ描画に使用するリソース
		FrameBatchResourcePool<MeshBatchResources> resourcePool_{};
		FrameBatchResourcePool<DxConstBuffer<SubMeshConstants>> subMeshCBPool_{};

		// メッシュのGPUリソース管理クラス
		MeshGPUResourceManager meshResourceManager_{};

		std::vector<GraphicsBindItem> bindScratch_{};
		std::vector<GraphicsBindItem> subMeshBindScratch_{};
		std::vector<ComputeBindItem> computeBindScratch_{};

		// 描画パス
		std::vector<std::unique_ptr<IMeshDrawPath>> drawPaths_{};

		// スキニング処理に使用するパイプライン
		AssetID skinningPipeline_{};
		// スキンメッシュのバッチキャッシュ
		std::unordered_map<SkinnedBatchCacheKey, MeshBatchResources*, SkinnedBatchCacheKeyHash> skinnedBatchCache_{};
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
		uint64_t BuildStaticBatchHash(const RenderDrawContext& context,
			std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) const;
		// 長時間使われていない静的バッチを破棄する
		void PruneStaticBatchCache();
		// スキンメッシュのGPUディスパッチ
		void DispatchSkinning(const RenderDrawContext& context, const MeshPreparedBatch& prepared);

		// スキンメッシュのリソースをキャッシュに登録する
		void RegisterSkinnedSources(AssetID meshAssetID, MeshBatchResources& resources,
			std::span<const RenderItem* const> items);
	};
} // Engine
