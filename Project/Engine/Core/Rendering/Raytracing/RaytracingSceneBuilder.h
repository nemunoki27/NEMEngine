#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h> 
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>

// c++
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class AssetDatabase;
	class MeshRenderBackend;
	class BufferUploadService;
	class SRVDescriptor;
	class PrimitiveGeometryManager;
	struct SceneExecutionContext;
	struct MeshRendererComponent;
	struct FillMeshRendererComponent;
	struct PrimitiveRendererComponent;

	//============================================================================
	//	RaytracingSceneBuilder structures
	//============================================================================
	// メッシピック記録用
	struct MeshSubMeshPickRecord {

		Entity entity = Entity::Null();
		uint32_t subMeshIndex = 0;
		UUID subMeshStableID{};
	};

	//============================================================================
	//	RaytracingSceneBuilder class
	//	レイトレーシングシーンの構築クラス
	//============================================================================
	class RaytracingSceneBuilder {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RaytracingSceneBuilder() = default;
		~RaytracingSceneBuilder() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始処理
		void BeginFrame(GraphicsCore& graphicsCore);

		// シーンの構築
		void BuildForScene(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			MeshRenderBackend* meshBackend, PrimitiveGeometryManager* primitiveGeometryManager,
			const RenderSceneBatch& renderBatch, SceneExecutionContext& context);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetTLASResource() const { return tlas_.GetResource(); }

		const std::vector<MeshSubMeshPickRecord>& GetPickRecords() const { return scenePickRecords_; }
		const std::vector<uint32_t>& GetPickRecordOffsets() const { return scenePickRecordOffsets_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- stricture ----------------------------------------------------

		// BLASのキー
		struct BLASKey {

			AssetID meshAssetID{};
			// ホットリロード世代、差し替えで別キーになり古いBLASを再利用しない
			uint32_t reloadGeneration = 0;
			// サブメッシュローカル行列を含むジオメトリ配置
			uint64_t geometryLayoutHash = 0;

			bool operator==(const BLASKey& rhs) const noexcept {
				return meshAssetID == rhs.meshAssetID &&
					reloadGeneration == rhs.reloadGeneration &&
					geometryLayoutHash == rhs.geometryLayoutHash;
			}
		};
		struct BLASKeyHash {
			size_t operator()(const BLASKey& key) const noexcept {
				const size_t h0 = std::hash<AssetID>{}(key.meshAssetID);
				const size_t h1 = std::hash<uint32_t>{}(key.reloadGeneration);
				const size_t h2 = std::hash<uint64_t>{}(key.geometryLayoutHash);
				size_t h = h0 ^ (h1 + 0x9e3779b9u + (h0 << 6) + (h0 >> 2));
				return h ^ (h2 + 0x9e3779b9u + (h << 6) + (h >> 2));
			}
		};
		// BLASのコレクション
		struct CollectedMeshInstance {

			AssetID meshAssetID{};
			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const MeshRendererComponent* renderer = nullptr;
		};
		// FillMeshのコレクション
		struct CollectedFillMeshInstance {

			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const FillMeshRendererComponent* renderer = nullptr;
		};
		// Primitiveのコレクション
		struct CollectedPrimitiveInstance {

			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const PrimitiveRendererComponent* renderer = nullptr;
			// 形状ハッシュ、共有ジオメトリのキー
			uint64_t geometryHash = 0;
		};
		// FillMeshのRTリソースキー
		struct FillMeshRTKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();

			bool operator==(const FillMeshRTKey& rhs) const noexcept {
				return world == rhs.world && entity.index == rhs.entity.index &&
					entity.generation == rhs.entity.generation;
			}
		};
		struct FillMeshRTKeyHash {
			size_t operator()(const FillMeshRTKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
				h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
				return h;
			}
		};
		// FillMeshのレイトレ用GPUリソース
		struct FillMeshRaytracingResource {

			MeshStructuredHandle<MeshVertex> vertexSRV{};
			MeshStructuredHandle<uint32_t> indexSRV{};
			ImmutableIndexBuffer indexBuffer{};
			BottomLevelAccelerationStructure blas{};
			uint32_t builtGeneration = 0;

			// SRVを解放する
			void Release(SRVDescriptor* srvDescriptor) {
				vertexSRV.Release(srvDescriptor);
				indexSRV.Release(srvDescriptor);
			}
		};
		// 動的BLASのキー
		struct DynamicBLASKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			// ホットリロード世代、差し替えで別キーになり古いBLASを再利用しない
			uint32_t reloadGeneration = 0;

			bool operator==(const DynamicBLASKey& rhs) const noexcept {
				return world == rhs.world && entity.index == rhs.entity.index && entity.generation == rhs.entity.generation &&
					meshAssetID == rhs.meshAssetID && reloadGeneration == rhs.reloadGeneration;
			}
		};
		struct DynamicBLASKeyHash {
			size_t operator()(const DynamicBLASKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
				h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
				h ^= (std::hash<AssetID>{}(key.meshAssetID) << 3);
				h ^= (std::hash<uint32_t>{}(key.reloadGeneration) << 4);
				return h;
			}
		};
		struct DynamicBLASEntry {

			BottomLevelAccelerationStructure blas{};
			uint64_t poseGeneration = 0;
			uint64_t bufferGeneration = 0;
			uint64_t geometryLayoutHash = 0;
			D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
		};

		//--------- variables ----------------------------------------------------

		// BLAS
		std::unordered_map<BLASKey, BottomLevelAccelerationStructure, BLASKeyHash> blases_;
		std::unordered_map<DynamicBLASKey,
			DynamicBLASEntry, DynamicBLASKeyHash> dynamicBlases_{};
		std::unordered_map<FillMeshRTKey, FillMeshRaytracingResource, FillMeshRTKeyHash> fillMeshRTResources_{};
		// メッシュごとに最後に構築したリロード世代、変化時に旧世代BLASを破棄する
		std::unordered_map<AssetID, uint32_t> meshBlasGeneration_;
		// TLAS
		TopLevelAccelerationStructure tlas_;

		// シーンインスタンスバッファ
		StructuredInstanceBuffer<RaytracingInstanceShaderData> sceneInstances_{ "gRaytracingSceneInstances" };
		// BLAS内ジオメトリバッファ
		StructuredInstanceBuffer<RaytracingGeometryShaderData> sceneGeometries_{ "gRaytracingGeometries" };
		// サブメッシュインスタンスバッファ
		StructuredInstanceBuffer<MeshSubMeshShaderData> sceneSubMeshes_{ "gRaytracingSubMeshes" };

		// インスタンスデータ
		std::vector<RaytracingInstanceShaderData> sceneInstanceScratch_{};
		std::vector<RaytracingGeometryShaderData> sceneGeometryScratch_{};
		std::vector<MeshSubMeshShaderData> sceneSubMeshScratch_{};

		// メッシュピック用のサブメッシュ情報
		std::vector<MeshSubMeshPickRecord> scenePickRecords_{};
		// TLASインスタンスごとのピック記録先頭
		std::vector<uint32_t> scenePickRecordOffsets_{};

		// テクスチャ解決キャッシュ
		mutable std::unordered_map<AssetID, std::string> textureKeyCache_{};
		mutable std::unordered_map<AssetID, uint32_t> textureDescriptorIndexCache_{};
		SRVDescriptor* srvDescriptor_ = nullptr;

		// 初期化済みか
		bool initialized_ = false;
		// 初回のTLAS構築か
		bool firstTLASBuild_ = true;
		// 前回TLASへ渡したインスタンス配置のハッシュ
		uint64_t tlasInstanceHash_ = 0;

		// 1フレームで二重構築しないための制御フラグ
		bool builtThisFrame_ = false;
		UUID builtSceneInstanceID_{};

		//--------- functions ----------------------------------------------------

		// 可視メッシュインスタンスの収集
		void CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances);
		// 可視FillMeshインスタンスの収集
		void CollectSceneFillMeshInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedFillMeshInstance>& outInstances);
		// 可視Primitiveインスタンスの収集
		void CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedPrimitiveInstance>& outInstances);
		// FillMeshのレイトレ用GPUリソースを構築する
		bool BuildFillMeshRaytracingResource(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			BufferUploadService& uploadService, const CollectedFillMeshInstance& src,
			FillMeshRaytracingResource& resource);
		// テクスチャデスクリプタインデックスの解決
		uint32_t ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
			AssetDatabase& assetDatabase, AssetID textureAssetID) const;
		// TLAS更新が必要か判定するためインスタンス配置をハッシュ化する
		static uint64_t ComputeTLASInstanceHash(
			std::span<const RaytracingTLASInstance> instances);
		// 既に構築済みのシーン情報を各ビューコンテキストに渡す
		void PublishBuiltScene(SceneExecutionContext& context) const;

	};
} // Engine

