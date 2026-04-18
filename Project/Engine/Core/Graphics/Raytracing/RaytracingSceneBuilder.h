#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Graphics/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Graphics/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>
#include <Engine/Core/Graphics/Render/Backend/Common/StructuredInstanceBuffer.h> 
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>

// c++
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class AssetDatabase;
	class MeshRenderBackend;
	struct SceneExecutionContext;
	struct MeshRendererComponent;

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
		//========================================================================
		//	public Methods
		//========================================================================

		RaytracingSceneBuilder() = default;
		~RaytracingSceneBuilder() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始処理
		void BeginFrame(GraphicsCore& graphicsCore);

		// シーンの構築
		void BuildForScene(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
			MeshRenderBackend* meshBackend, const RenderSceneBatch& renderBatch, SceneExecutionContext& context);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetTLASResource() const { return tlas_.GetResource(); }

		const std::vector<MeshSubMeshPickRecord>& GetPickRecords() const { return scenePickRecords_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- stricture ----------------------------------------------------

		// BLASのキー
		struct BLASKey {

			AssetID meshAssetID{};
			uint32_t subMeshIndex = 0;

			bool operator==(const BLASKey& rhs) const noexcept {
				return meshAssetID == rhs.meshAssetID && subMeshIndex == rhs.subMeshIndex;
			}
		};
		struct BLASKeyHash {
			size_t operator()(const BLASKey& key) const noexcept {
				const size_t h0 = std::hash<AssetID>{}(key.meshAssetID);
				const size_t h1 = std::hash<uint32_t>{}(key.subMeshIndex);
				return h0 ^ (h1 + 0x9e3779b9u + (h0 << 6) + (h0 >> 2));
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
		// 動的BLASのキー
		struct DynamicBLASKey {

			ECSWorld* world = nullptr;
			Entity entity = Entity::Null();
			AssetID meshAssetID{};
			uint32_t subMeshIndex = 0;

			bool operator==(const DynamicBLASKey& rhs) const noexcept {
				return world == rhs.world && entity.index == rhs.entity.index && entity.generation == rhs.entity.generation &&
					meshAssetID == rhs.meshAssetID && subMeshIndex == rhs.subMeshIndex;
			}
		};
		struct DynamicBLASKeyHash {
			size_t operator()(const DynamicBLASKey& key) const noexcept {
				size_t h = std::hash<void*>{}(key.world);
				h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
				h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
				h ^= (std::hash<AssetID>{}(key.meshAssetID) << 3);
				h ^= (std::hash<uint32_t>{}(key.subMeshIndex) << 4);
				return h;
			}
		};

		//--------- variables ----------------------------------------------------

		// BLAS
		std::unordered_map<BLASKey, BottomLevelAccelerationStructure, BLASKeyHash> blases_;
		std::unordered_map<DynamicBLASKey, BottomLevelAccelerationStructure, DynamicBLASKeyHash> dynamicBlases_{};
		// TLAS
		TopLevelAccelerationStructure tlas_;

		// シーンインスタンスバッファ
		StructuredInstanceBuffer<RaytracingInstanceShaderData> sceneInstances_{ "gRaytracingSceneInstances" };
		// サブメッシュインスタンスバッファ
		StructuredInstanceBuffer<MeshSubMeshShaderData> sceneSubMeshes_{ "gRaytracingSubMeshes" };

		// インスタンスデータ
		std::vector<RaytracingInstanceShaderData> sceneInstanceScratch_{};
		std::vector<MeshSubMeshShaderData> sceneSubMeshScratch_{};

		// メッシュピック用のサブメッシュ情報
		std::vector<MeshSubMeshPickRecord> scenePickRecords_{};

		// テクスチャ解決キャッシュ
		mutable std::unordered_map<AssetID, std::string> textureKeyCache_{};
		mutable std::unordered_map<AssetID, uint32_t> textureDescriptorIndexCache_{};

		// 初期化済みか
		bool initialized_ = false;
		// 初回のTLAS構築か
		bool firstTLASBuild_ = true;

		// 1フレームで二重構築しないための制御フラグ
		bool builtThisFrame_ = false;
		UUID builtSceneInstanceID_{};

		//--------- functions ----------------------------------------------------

		// 可視メッシュインスタンスの収集
		void CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
			const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances);
		// テクスチャデスクリプタインデックスの解決
		uint32_t ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
			AssetDatabase& assetDatabase, AssetID textureAssetID) const;
		// 既に構築済みのシーン情報を各ビューコンテキストに渡す
		void PublishBuiltScene(SceneExecutionContext& context) const;

	};
} // Engine