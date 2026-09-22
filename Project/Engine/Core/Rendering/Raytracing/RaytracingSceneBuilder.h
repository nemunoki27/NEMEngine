#pragma once

//============================================================================
//	include
//============================================================================
#include "RaytracingTLASState.h"
#include "RaytracingMaterialResolver.h"
#include "RaytracingSceneResult.h"
#include "RaytracingBLASCache.h"
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class AssetDatabase;
	class MeshRenderBackend;
	class RenderAssetLibrary;
	class MaterialResolver;
	class MaterialParameterSet;
	class SRVDescriptor;
	class PrimitiveGeometryManager;
	struct MaterialAsset;
	struct SceneExecutionContext;
	struct MeshRendererComponent;
	struct PrimitiveRendererComponent;

	//============================================================================
	//	RaytracingSceneBuilder structures
	//============================================================================
	// メッシピック記録用

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
			RenderAssetLibrary& assetLibrary, MaterialResolver& materialResolver,
			MeshRenderBackend* meshBackend, PrimitiveGeometryManager* primitiveGeometryManager,
			const RenderSceneBatch& renderBatch, SceneExecutionContext& context);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetTLASResource() const { return tlasState_.GetResource(); }

		const std::vector<MeshSubMeshPickRecord>& GetPickRecords() const { return result_.scenePickRecords_; }
		const std::vector<uint32_t>& GetPickRecordOffsets() const { return result_.scenePickRecordOffsets_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		using BLASKey = RaytracingBLASCache::BLASKey;
		using BLASKeyHash = RaytracingBLASCache::BLASKeyHash;
		using StaticInstanceBLASKey = RaytracingBLASCache::StaticInstanceBLASKey;
		using StaticInstanceBLASKeyHash = RaytracingBLASCache::StaticInstanceBLASKeyHash;
		using StaticInstanceBLASEntry = RaytracingBLASCache::StaticInstanceBLASEntry;
		using DynamicBLASKey = RaytracingBLASCache::DynamicBLASKey;
		using DynamicBLASKeyHash = RaytracingBLASCache::DynamicBLASKeyHash;
		using DynamicBLASEntry = RaytracingBLASCache::DynamicBLASEntry;

		//--------- stricture ----------------------------------------------------

		// BLASのキー

		// サブメッシュ固有変換を持つ静的メッシュはEntity単位でrefitする

		// 静的メッシュのLOD差分更新に必要なインスタンス情報
		struct CachedMeshLODInstance {

			AssetID meshAssetID{};
			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			uint32_t reloadGeneration = 0;
			uint64_t geometryLayoutHash = 0;
			bool tracksInstanceLayout = false;
			bool usesInstanceBLAS = false;
			uint32_t tlasInstanceIndex = 0;
			uint32_t geometryDataOffset = 0;
			uint32_t geometryCount = 0;
			uint32_t lodIndex = 0;
			Vector3 worldBoundsCenter = Vector3::AnyInit(0.0f);
			float worldBoundsRadius = 0.0f;
		};
		// BLASのコレクション
		struct CollectedMeshInstance {

			AssetID meshAssetID{};
			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const MeshRendererComponent* renderer = nullptr;
			bool castShadows = true;
		};
		// Primitiveのコレクション
		struct CollectedPrimitiveInstance {

			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const PrimitiveRendererComponent* renderer = nullptr;
			const MaterialParameterSet* materialInstance = nullptr;
			AssetID material{};
			MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
			Matrix4x4 uvMatrix = Matrix4x4::Identity();
			bool castShadows = true;
			// 形状ハッシュ、共有ジオメトリのキー
			uint64_t geometryHash = 0;
		};
		// TLASインスタンスをEntityから検索するためのキー
		struct SceneEntityKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();

			bool operator==(const SceneEntityKey& rhs) const noexcept { return world == rhs.world && entity == rhs.entity; }
		};
		struct SceneEntityKeyHash {
			size_t operator()(
				const SceneEntityKey& key) const noexcept;
		};

		struct SceneBuildWork {

			GraphicsCore& graphicsCore;
			AssetDatabase& assetDatabase;
			RenderAssetLibrary& assetLibrary;
			MaterialResolver& materialResolver;
			MeshRenderBackend* meshBackend;
			PrimitiveGeometryManager* primitiveGeometryManager;
			const GraphicsRuntimeFeatures& runtimeFeatures;
			const ResolvedRenderView* lodView;
			ID3D12Device8* device;
			ID3D12GraphicsCommandList6* commandList;
			std::vector<RaytracingTLASInstance>& tlasInstances;
			std::vector<SceneEntityKey>& tlasEntityKeys;
			std::vector<CachedMeshLODInstance>& meshLODInstances;
			std::vector<uint32_t>& meshLODRecordIndices;
			bool& requireTlasRebuild;
			bool& staticScene;
			uint32_t& blasGeometryCount;
		};

		//--------- variables ----------------------------------------------------

		// BLAS
		RaytracingBLASCache blasCache_{};
		// TLAS
		RaytracingTLASState tlasState_{};

		RaytracingSceneResult result_{};

		// テクスチャ解決キャッシュ
		RaytracingMaterialResolver materialResolver_{};
		// 反射履歴の無効化に使うマテリアル内容の世代
		uint64_t sceneMaterialGeneration_ = 0;
		uint64_t cachedSceneMaterialHash_ = 0;

		// 初期化済みか
		bool initialized_ = false;
		// 静的シーンのTransform差分更新に使うTLAS配置
		std::vector<RaytracingTLASInstance>
			cachedTLASInstances_{};
		std::unordered_multimap<SceneEntityKey, uint32_t,
			SceneEntityKeyHash> cachedTLASInstanceIndices_{};
		std::vector<CachedMeshLODInstance>
			cachedMeshLODInstances_{};
		// TLASインスタンスからLODキャッシュをO(1)で参照する
		std::vector<uint32_t>
			cachedMeshLODRecordIndices_{};

		// 1フレームで二重構築しないための制御フラグ
		bool builtThisFrame_ = false;
		const ECSWorld* builtWorld_ = nullptr;
		UUID builtSceneInstanceID_{};
		// 静的シーンはWorldとMesh GPUリソースが変わるまでCPU構築結果を再利用する
		bool cachedStaticScene_ = false;
		const ECSWorld* cachedWorld_ = nullptr;
		UUID cachedSceneInstanceID_{};
		uint64_t cachedRenderRevision_ = 0;
		uint64_t cachedTransformRevision_ = 0;
		uint64_t cachedMeshResourceRevision_ = 0;
		uint64_t cachedLODViewHash_ = 0;
		uint32_t cachedBLASGeometryCount_ = 0;
		uint32_t cachedTLASInstanceCount_ = 0;

		//--------- functions ----------------------------------------------------

		// MeshのBLASとインスタンスを構築する
		void BuildMeshInstances(std::span<const CollectedMeshInstance> sceneMeshes, SceneBuildWork& work);
		// PrimitiveのBLASとインスタンスを構築する
		void BuildPrimitiveInstances(std::span<const CollectedPrimitiveInstance> scenePrimitives, SceneBuildWork& work);

		// 可視メッシュインスタンスの収集
		void CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances);
		// 可視Primitiveインスタンスの収集
		void CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedPrimitiveInstance>& outInstances);

		// 既に構築済みのシーン情報を各ビューコンテキストに渡す
		void PublishBuiltScene(SceneExecutionContext& context) const;

	};
} // Engine
