#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Graphics/Render/Backend/Common/ViewConstantBuffer.h>
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshSkinningSharedTypes.h>
#include <Engine/Core/Graphics/GPUBuffer/StructuredRWBuffer.h>
#include <Engine/Core/ECS/Entity/Entity.h>

// c++
#include <vector>
#include <span>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;
	struct RenderDrawContext;
	struct MeshGPUResource;
	struct MeshRendererComponent;

	//============================================================================
	//	MeshBatchResources structures
	//============================================================================

	// 定数バッファ
	struct MeshViewConstants {

		Matrix4x4 viewProjection = Matrix4x4::Identity();
	};
	// 頂点/メッシュシェーダインスタンスデータ
	struct MeshInstanceData {

		// エンティティワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();

		// このインスタンスのサブメッシュ配列先頭
		uint32_t subMeshDataOffset = 0;
		uint32_t subMeshCount = 0;

		// スキニングするか
		uint32_t flags = 0;
		// スキニングする場合の、スキン頂点配列のオフセット
		uint32_t skinnedVertexOffset = 0;
	};
	// MeshInstanceDataのflagsで、スキニングするか
	static constexpr uint32_t kMeshInstanceFlagSkinned = 1u;

	// ピクセルシェーダインスタンスデータ
	struct MeshPSInstanceData {

		// サブメッシュごとに設定しているため今はなし
		Color testColor = Color::White();
	};

	// スキニングメッシュを持つエンティティの記録
	struct SkinnedEntityRecord {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();
		uint32_t vertexOffset = 0;
	};
	// スキニングするエンティティを検索するためのキー
	struct SkinnedEntityLookupKey {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();

		bool operator==(const SkinnedEntityLookupKey& rhs) const noexcept {
			return world == rhs.world && entity.index == rhs.entity.index &&
				entity.generation == rhs.entity.generation;
		}
	};
	struct SkinnedEntityLookupKeyHash {
		size_t operator()(const SkinnedEntityLookupKey& key) const noexcept {
			size_t h = std::hash<void*>{}(key.world);
			h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
			h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
			return h;
		}
	};

	//============================================================================
	//	MeshBatchResources class
	//	メッシュをインスタンシングで描画するためのリソースを管理するクラス
	//============================================================================
	class MeshBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshBatchResources() = default;
		~MeshBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 描画に使用するビューを更新する
		void UpdateView(const ResolvedRenderView& view);
		void UploadBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh);

		// スキニングに使用するリソースを確保する
		void EnsureSkinningResources(GraphicsCore& graphicsCore);

		//--------- accessor -----------------------------------------------------

		// スキニングするインスタンスの頂点オフセットを取得する
		bool FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const;

		// スキニング処理をディスパッチしたかをセット
		void SetSkinningDispatched(bool value) { skinningDispatched_ = value; }
		// スキニング頂点のリソース状態をセット
		void SetSkinnedVertexState(D3D12_RESOURCE_STATES state) { skinning_->skinnedVertexState = state; }

		// スキニング用のリソースがあるか
		bool HasSkinningResources() const { return skinning_ != nullptr; }

		// 内部リソースを取得する
		ID3D12Resource* GetSkinnedVerticesResource() const { return  skinning_->skinnedVertices.GetResource(); }

		// GPUアドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress(RenderViewKind kind) const { return view_[ToViewIndex(kind)].GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstanceMeshGPUAddress() const { return meshData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstancePSGPUAddress() const { return psData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return draw_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshGPUAddress() const { return subMeshData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningPaletteGPUAddress() const { return skinning_->skinningPalette.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningConstantsGPUAddress() const { return skinning_->skinningConstants.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedVerticesGPUAddress() const { return skinning_->skinnedVertices.GetGPUAddress(); }

		// SRV/UAVハンドルを取得する
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesSRVHandle() const { return skinning_->skinnedVertices.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesUAVHandle() const { return skinning_->skinnedVertices.GetUAVGPUHandle(); }

		// 描画バウンディング名を取得する
		std::string_view GetViewBindingName() const { return "ViewConstants"; }
		std::string_view GetInstanceMeshBindingName() const { return meshData_.GetBindingName(); }
		std::string_view GetInstancePSBindingName() const { return psData_.GetBindingName(); }
		std::string_view GetDrawBindingName() const { return draw_.GetBindingName(); }
		std::string_view GetSubMeshBindingName() const { return subMeshData_.GetBindingName(); }
		std::string_view GetSkinningPaletteBindingName() const { return "gSkinningPalette"; }
		std::string_view GetSkinningConstantsBindingName() const { return "SkinningConstants"; }
		std::string_view GetSkinnedVerticesBindingName() const { return "gSkinnedVertices"; }

		// スキニング頂点バッファのSRVインデックスを取得する
		uint32_t GetSkinnedVerticesSRVIndex() const { return skinning_->skinnedVertices.GetSRVIndex(); }

		// インスタンス数を取得する
		uint32_t GetInstanceCount() const { return instanceCount_; }
		uint32_t GetSkinnedInstanceCount() const { return skinnedInstanceCount_; }

		// スキニング処理をディスパッチしたか
		bool IsSkinningDispatched() const { return skinningDispatched_; }
		// スキニング頂点のリソース状態を取得する
		D3D12_RESOURCE_STATES GetSkinnedVertexState() const { return skinning_->skinnedVertexState; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// スキニング用のリソースをまとめた構造体
		struct OptionalSkinningResources {

			StructuredInstanceBuffer<WellForGPU> skinningPalette{ "gSkinningPalette" };
			StructuredRWBuffer<MeshVertex> skinnedVertices{ "gSkinnedVertices" };
			ViewConstantBuffer<MeshSkinningDispatchConstants> skinningConstants{ "SkinningConstants" };

			D3D12_RESOURCE_STATES skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		};

		//--------- variables ----------------------------------------------------

		// ビューバッファ
		std::array<ViewConstantBuffer<MeshViewConstants>, 2> view_ = {
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" },
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" }
		};

		// バッファ
		StructuredInstanceBuffer<MeshInstanceData> meshData_{ "gMeshInstances" };
		StructuredInstanceBuffer<MeshPSInstanceData> psData_{ "gPSInstances" };
		ViewConstantBuffer<MeshDrawConstants> draw_{ "MeshDrawConstants" };
		StructuredInstanceBuffer<MeshSubMeshShaderData> subMeshData_{ "gSubMeshes" };

		// スキニング用バッファ
		std::unique_ptr<OptionalSkinningResources> skinning_{};

		// 毎バッチ再利用するデータ
		std::vector<MeshInstanceData> meshScratch_{};
		std::vector<MeshPSInstanceData> psScratch_{};
		std::vector<MeshSubMeshShaderData> subMeshScratch_{};

		// スキニング用の毎バッチ再利用するデータ
		std::vector<WellForGPU> paletteScratch_{};

		// スキニングメッシュを持つエンティティの記録
		std::vector<SkinnedEntityRecord> skinnedRecords_{};
		std::unordered_map<SkinnedEntityLookupKey, uint32_t, SkinnedEntityLookupKeyHash> skinnedVertexOffsetMap_{};

		// インスタンス数
		uint32_t instanceCount_ = 0;
		uint32_t skinnedInstanceCount_ = 0;

		// 初期化状態
		bool initialized_ = false;

		// スキニング処理をディスパッチしたか
		bool skinningDispatched_ = false;

		//--------- functions ----------------------------------------------------

		static constexpr size_t ToViewIndex(RenderViewKind kind) { return static_cast<size_t>(kind); }
	};
} // Engine