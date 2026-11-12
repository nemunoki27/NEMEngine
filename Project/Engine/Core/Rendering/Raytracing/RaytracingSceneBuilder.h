#pragma once

//============================================================================
//	include
//============================================================================
#include "RaytracingTLASState.h"
#include "RaytracingMaterialResolver.h"
#include "RaytracingSceneResult.h"
#include "RaytracingBLASCache.h"
#include <Engine/Core/World/ECS/Entity/WorldEntityKey.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	// 前方宣言
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
	struct SceneInstance;
	struct MeshRendererComponent;
	struct PrimitiveRendererComponent;
	struct LightInfluenceSettings;

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
		void BuildForScene(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, RenderAssetLibrary& assetLibrary,
			MaterialResolver& materialResolver, MeshRenderBackend* meshBackend,
			PrimitiveGeometryManager* primitiveGeometryManager, const RenderSceneBatch& renderBatch,
			SceneExecutionContext& context);

		// 現在のTLASからライトの除外対象を追加する
		void AppendShadowExclusions(const LightInfluenceSettings& settings, const ECSWorld& world, Entity owner,
			std::vector<uint32_t>& out) const;

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetTLASResource() const { return scene_.tlasState.GetResource(); }
		uint64_t GetStaticBLASOriginalBytes() const { return blasCache_.GetOriginalBytes(); }
		uint64_t GetStaticBLASPublishedBytes() const { return blasCache_.GetPublishedBytes(); }

		const std::vector<MeshSubMeshPickRecord>& GetPickRecords() const { return scene_.result.scenePickRecords_; }
		const std::vector<uint32_t>& GetPickRecordOffsets() const { return scene_.result.scenePickRecordOffsets_; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		using BLASKey = RaytracingBLASCache::BLASKey;
		using BLASKeyHash = RaytracingBLASCache::BLASKeyHash;
		using StaticInstanceBLASKey = RaytracingBLASCache::StaticInstanceBLASKey;
		using StaticInstanceBLASKeyHash = RaytracingBLASCache::StaticInstanceBLASKeyHash;
		using StaticInstanceBLASEntry = RaytracingBLASCache::StaticInstanceBLASEntry;
		using DynamicBLASKey = RaytracingBLASCache::DynamicBLASKey;
		using DynamicBLASKeyHash = RaytracingBLASCache::DynamicBLASKeyHash;
		using DynamicBLASEntry = RaytracingBLASCache::DynamicBLASEntry;

		// 静的Meshの固有BLASを保持するための情報
		struct CachedMeshInstance {

			AssetID meshAssetID{};
			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			uint32_t reloadGeneration = 0;
			bool tracksInstanceLayout = false;
		};
		// MeshのBLAS構築に使う描画情報
		struct CollectedMeshInstance {

			AssetID meshAssetID{};
			Entity entity = Entity::Null();
			ECSWorld* world = nullptr;
			Matrix4x4 worldMatrix = Matrix4x4::Identity();
			const MeshRendererComponent* renderer = nullptr;
			bool castShadows = true;
			bool viewDependent = false;
		};
		// PrimitiveのBLAS構築に使う描画情報
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
			bool viewDependent = false;
		};
		// WorldとEntityの共通検索キー
		using SceneEntityKey = WorldEntityKey;
		using SceneEntityKeyHash = WorldEntityKeyHash;

		// TLASと転送結果の再利用条件を保持する
		struct SceneState {

			RaytracingTLASState tlasState{};
			RaytracingSceneResult result{};
			std::vector<std::shared_ptr<const uint8_t>> buildingMeshOwnerships;
			std::vector<std::shared_ptr<const uint8_t>> publishedMeshOwnerships;
			std::vector<BLASKey> publishedBLASKeys;
			std::unordered_set<AssetID> renderTextureInputs;
			RaytracingMaterialResolver materialResolver{};
			uint64_t sceneMaterialGeneration = 0;
			uint64_t cachedSceneMaterialHash = 0;
			std::vector<RaytracingTLASInstance> cachedTLASInstances;
			std::vector<WorldEntityKey> shadowInstanceEntities;
			std::unordered_multimap<SceneEntityKey, uint32_t, SceneEntityKeyHash> cachedTLASInstanceIndices;
			std::vector<CachedMeshInstance> cachedMeshInstances;
			bool builtThisFrame = false;
			const ECSWorld* builtWorld = nullptr;
			std::shared_ptr<const ECSWorldLifetime> builtWorldLifetime;
			uint64_t builtRenderRevision = 0;
			uint64_t builtMeshResourceRevision = 0;
			const ResolvedRenderView* builtView = nullptr;
			UUID builtSceneInstanceID{};
			bool cachedStaticScene = false;
			const ECSWorld* cachedWorld = nullptr;
			std::shared_ptr<const ECSWorldLifetime> cachedWorldLifetime;
			std::shared_ptr<const RegistryRevision> cachedExtractorRevision;
			UUID cachedSceneInstanceID{};
			uint64_t cachedRenderRevision = 0;
			uint64_t cachedTransformRevision = 0;
			uint64_t cachedMeshResourceRevision = 0;
			uint64_t cachedGeometryContentHash = 0;
			uint64_t textureRevision = 0;
			std::shared_ptr<const uint64_t> materialRevision;
			uint32_t cachedBLASGeometryCount = 0;
			uint32_t cachedTLASInstanceCount = 0;
		};

		// Scene構築中だけ借用する処理対象と出力
		struct SceneBuildWork {

			GraphicsCore& graphicsCore;
			SceneState& scene;
			const GraphicsRuntimeFeatures& runtimeFeatures;
			AssetDatabase& assetDatabase;
			RenderAssetLibrary& assetLibrary;
			MaterialResolver& materialResolver;
			MeshRenderBackend* meshBackend;
			PrimitiveGeometryManager* primitiveGeometryManager;
			ID3D12Device8* device;
			ID3D12GraphicsCommandList6* commandList;
			std::vector<RaytracingTLASInstance>& tlasInstances;
			std::vector<SceneEntityKey>& tlasEntityKeys;
			std::vector<CachedMeshInstance>& meshInstances;
			bool& requireTLASRebuild;
			bool& blasContentsChanged;
			uint64_t& geometryContentHash;
			bool& staticScene;
			uint32_t& blasGeometryCount;
		};

		//--------- variables ----------------------------------------------------

		// Sceneの共有形状と描画結果
		RaytracingBLASCache blasCache_{};
		SceneState scene_{};
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// MeshのBLASとインスタンスを構築する
		void BuildMeshInstances(std::span<const CollectedMeshInstance> sceneMeshes, SceneBuildWork& work);
		// PrimitiveのBLASとインスタンスを構築する
		void BuildPrimitiveInstances(std::span<const CollectedPrimitiveInstance> scenePrimitives, SceneBuildWork& work);

		// 可視メッシュインスタンスの収集
		void CollectSceneMeshInstances(const RenderSceneBatch& renderBatch, const SceneInstance& scene,
			const ResolvedRenderView* view, std::vector<CollectedMeshInstance>& outInstances);
		// 可視Primitiveを収集
		void CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch, const SceneInstance& scene,
			const ResolvedRenderView* view, std::vector<CollectedPrimitiveInstance>& outInstances);

		// 構築結果をViewへ渡す
		void PublishBuiltScene(SceneExecutionContext& context, SceneState& state);
	};
}
