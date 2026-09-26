#pragma once

//============================================================================
//	include
//============================================================================
#include "MeshMaterialBuffers.h"
#include "MeshBatchViewResources.h"
#include "MeshSkinningBufferSet.h"
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshSkinningSharedTypes.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <array>
#include <memory>
#include <vector>
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;
	class SRVDescriptor;
	struct RenderDrawContext;
	struct MeshGPUResource;
	struct MeshRendererComponent;

	//============================================================================
	//	MeshBatchResources structures
	//============================================================================
	// 定数バッファ

	static_assert(sizeof(MeshViewConstants) % 16 == 0);

	// 頂点/メッシュシェーダインスタンスデータ
	struct MeshInstanceData {

		// エンティティワールド行列(位置・Bounds・Culling用)
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		// worldMatrixの法線変換行列transpose(inverse(worldMatrix))
		// 非一様スケール・負スケールでも法線が壊れないよう位置用とは別に持つ
		Matrix4x4 normalMatrix = Matrix4x4::Identity();

		// このインスタンスのサブメッシュ配列先頭
		uint32_t subMeshDataOffset = 0;
		uint32_t subMeshCount = 0;

		// スキニングするか
		uint32_t flags = 0;
		// スキニングする場合の、スキン頂点配列のオフセット
		uint32_t skinnedVertexOffset = 0;

		// このインスタンスが参照するアウトラインGPUデータのインデックス
		uint32_t outlineDataIndex = 0;
		// worldMatrixの線形部の行列式の符号で負スケールのmirror時に-1
		float orientationSign = 1.0f;
		// エディターラスターピック用のEntity ID
		uint32_t entityIndex = UINT32_MAX;
		uint32_t entityGeneration = UINT32_MAX;

		// per-instanceの乗算色tint、同マテリアルのまま個体ごとに色を変えるために使う
		Color4 color = Color4::White();
		uint32_t motionFrameSerial = 0;
		uint32_t _motionPad[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(MeshInstanceData) % 16 == 0);
	// MeshInstanceDataのflagsで、スキニングするか
	static constexpr uint32_t kMeshInstanceFlagSkinned = 1u;
	// MeshRenderFlagsから写すピクセル側で参照するフラグ
	static constexpr uint32_t kMeshInstanceFlagLighting = 1u << 1;
	static constexpr uint32_t kMeshInstanceFlagReceiveShadow = 1u << 2;
	static constexpr uint32_t kMeshInstanceFlagReceiveIBL = 1u << 3;
	static constexpr uint32_t kMeshInstanceFlagReceiveReflection = 1u << 4;

	// スキニングメッシュを持つエンティティの記録
	struct SkinnedEntityRecord {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();
		uint32_t vertexOffset = 0;
	};
	// メッシュインスタンスをEntityから検索するためのキー
	struct MeshEntityLookupKey {

		ECSWorld* world = nullptr;
		Entity entity = Entity::Null();

		bool operator==(const MeshEntityLookupKey& rhs) const noexcept;
	};
	struct MeshEntityLookupKeyHash {
		size_t operator()(const MeshEntityLookupKey& key) const noexcept;
	};

	//============================================================================
	//	MeshBatchResources class
	//	メッシュをインスタンシングで描画するためのリソースを管理するクラス
	//============================================================================
	class MeshBatchResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshBatchResources() = default;
		~MeshBatchResources();

		// 初期化
		void Init(GraphicsCore& graphicsCore);
		// 終了処理
		void Finalize();

		// 描画に使用するビューを更新する
		void UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView);
		void UploadBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh);
		// 順序付きの描画構成とEntityの更新世代を照合する
		bool MatchesBatch(const RenderSceneBatch& batch, std::span<const RenderItem* const> items,
			const MeshGPUResource& gpuMesh) const;
		void CaptureBatchIdentity(const RenderSceneBatch& batch, std::span<const RenderItem* const> items,
			const MeshGPUResource& gpuMesh);
		// 色だけが変わったインスタンスのパラメータを更新する
		uint32_t RefreshMaterialColors();
		// 構成再抽出後も一致するバッチは行列だけ同期する
		void RefreshBatchTransforms(std::span<const RenderItem* const> items);
		// 静的バッチの構成を維持したままインスタンス行列だけを更新する
		bool RefreshInstanceTransforms(
			std::span<const RenderTransformChange> changes);
		// 静的キャッシュが保持するCPU配列を現在のフレーム用バッファへ転送する
		void UploadCachedBatchData();
		// 描画パスごとに変わるMeshDrawConstantsを毎描画更新しキャッシュヒット時も必ず呼ぶ
		void UpdateDrawConstants(const RenderDrawContext& drawContext,
			const MeshGPUResource& gpuMesh, uint32_t subMeshIndex,
			uint32_t subMeshGroupIndex, const MaterialAsset* material);
		// ExecuteIndirectで使用する頂点描画引数の定数を更新する
		void UpdateIndexedIndirectArgsConstants(uint32_t indexCount);

		// reflection駆動のサブメッシュ単位マテリアルパラメータを詰めて可変stride構造化バッファへ転送する
		void UploadSubMeshMaterialParams(const MaterialAsset* material,
			const MaterialParameterLayout& layout, const RenderDrawContext& drawContext);

		// スキニングに使用するリソースを確保する
		void EnsureSkinningResources(GraphicsCore& graphicsCore);

		//--------- accessor -----------------------------------------------------

		// スキニングするインスタンスの頂点オフセットを取得する
		bool FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const;

		// 現在のポーズに対するスキニング処理完了を記録する
		void MarkSkinningDispatched();
		// スキニング頂点のリソース状態をセット
		void SetSkinnedVertexState(D3D12_RESOURCE_STATES state) { skinning_->skinnedVertexState = state; }
		// 圧縮頂点側のスキニング結果も通常頂点とは別に状態管理する
		void SetSkinnedPackedVertexState(D3D12_RESOURCE_STATES state) { skinning_->skinnedPackedVertexState = state; }

		// スキニング用のリソースがあるか
		bool HasSkinningResources() const { return skinning_ != nullptr; }

		// 内部リソースを取得する
		ID3D12Resource* GetSkinnedVerticesResource() const { return  skinning_->skinnedVertices.GetResource(); }
		ID3D12Resource* GetSkinnedPackedVerticesResource() const { return  skinning_->skinnedPackedVertices.GetResource(); }

		// GPUアドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress(RenderViewKind kind) const { return viewResources_.GetViewGPUAddress(kind); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstanceMeshGPUAddress() const { return meshData_.GetGPUAddress(); }
		// カリングComputeが書き込み、ExecuteIndirect/ASが読む可視インスタンス配列
		D3D12_GPU_VIRTUAL_ADDRESS GetVisibleInstanceMeshGPUAddress() const { return visibleMeshData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return viewResources_.GetDrawGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndirectArgsConstantsGPUAddress() const { return viewResources_.GetIndirectGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshGPUAddress() const { return subMeshData_.GetGPUAddress(); }
		// reflection駆動のサブメッシュ単位マテリアルパラメータバッファ
		bool HasSubMeshMaterialParams() const { return materialBuffers_.IsAvailable(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshMaterialParamGPUAddress() const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetSubMeshMaterialParamGPUHandle() const;
		std::string_view GetSubMeshMaterialParamBindingName() const { return MaterialParameterCBuffer::kMesh; }
		// 背面法アウトラインのインスタンス別GPUデータ
		D3D12_GPU_VIRTUAL_ADDRESS GetOutlineGPUAddress() const { return outlineData_.GetGPUAddress(); }
		std::string_view GetOutlineBindingName() const { return outlineData_.GetBindingName(); }
		// ScreenSpaceOutline Mask描画用のper-draw定数(Style ID / SubMesh制限)
		D3D12_GPU_VIRTUAL_ADDRESS GetScreenSpaceOutlineMaskGPUAddress() const { return viewResources_.GetMaskGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningPaletteGPUAddress() const { return skinning_->skinningPalette.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningConstantsGPUAddress() const { return skinning_->skinningConstants.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedVerticesGPUAddress() const { return skinning_->skinnedVertices.GetGPUAddress(); }
		// MeshShader経路は圧縮頂点を読むため、スキニング後も圧縮頂点SRVを渡す
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedPackedVerticesGPUAddress() const { return skinning_->skinnedPackedVertices.GetGPUAddress(); }

		// SRV/UAVハンドルを取得する
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesSRVHandle() const { return skinning_->skinnedVertices.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedVerticesUAVHandle() const { return skinning_->skinnedVertices.GetUAVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedPackedVerticesSRVHandle() const { return skinning_->skinnedPackedVertices.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSkinnedPackedVerticesUAVHandle() const { return skinning_->skinnedPackedVertices.GetUAVGPUHandle(); }
		ID3D12Resource* GetIndexedIndirectArgsResource() const { return indexedIndirectArgs_.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndexedIndirectArgsGPUAddress() const { return indexedIndirectArgs_->GetGPUVirtualAddress(); }
		D3D12_RESOURCE_STATES GetIndexedIndirectArgsState() const { return indexedIndirectArgsState_; }
		void SetIndexedIndirectArgsState(D3D12_RESOURCE_STATES state) { indexedIndirectArgsState_ = state; }
		// カリング後に残ったインスタンスだけを格納するバッファ
		ID3D12Resource* GetVisibleInstanceMeshResource() const { return visibleMeshData_.GetResource(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetVisibleInstanceMeshSRVHandle() const { return visibleMeshData_.GetSRVGPUHandle(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetVisibleInstanceMeshUAVHandle() const { return visibleMeshData_.GetUAVGPUHandle(); }
		D3D12_RESOURCE_STATES GetVisibleInstanceMeshState() const { return visibleMeshDataState_; }
		void SetVisibleInstanceMeshState(D3D12_RESOURCE_STATES state) { visibleMeshDataState_ = state; }

		// 描画バウンディング名を取得する
		std::string_view GetViewBindingName() const { return "ViewConstants"; }
		std::string_view GetInstanceMeshBindingName() const { return meshData_.GetBindingName(); }
		std::string_view GetDrawBindingName() const { return "MeshDrawConstants"; }
		std::string_view GetIndirectArgsConstantsBindingName() const { return "IndirectArgsConstants"; }
		std::string_view GetSubMeshBindingName() const { return subMeshData_.GetBindingName(); }
		std::string_view GetSkinningPaletteBindingName() const { return "gSkinningPalette"; }
		std::string_view GetSkinningConstantsBindingName() const { return "SkinningConstants"; }
		std::string_view GetSkinnedVerticesBindingName() const { return "gSkinnedVertices"; }
		std::string_view GetSkinnedPackedVerticesBindingName() const { return "gSkinnedPackedVertices"; }

		// スキニング頂点バッファのSRVインデックスを取得する
		uint32_t GetSkinnedVerticesSRVIndex() const { return skinning_->skinnedVertices.GetSRVIndex(); }

		// インスタンス数を取得する
		uint32_t GetInstanceCount() const { return instanceCount_; }
		uint32_t GetSkinnedInstanceCount() const { return skinnedInstanceCount_; }
		uint64_t GetSkinningBufferGeneration() const { return skinningBufferGeneration_; }

		// スキニング処理をディスパッチしたか
		bool IsSkinningDispatched() const { return skinningDispatched_; }
		bool UsesFallbackTexture() const { return usesFallbackTexture_; }
		// スキニング頂点のリソース状態を取得する
		D3D12_RESOURCE_STATES GetSkinnedVertexState() const { return skinning_->skinnedVertexState; }
		D3D12_RESOURCE_STATES GetSkinnedPackedVertexState() const { return skinning_->skinnedPackedVertexState; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// スキニング用のリソースをまとめた構造体

		// 同一フレーム内で別パスが通常描画のUpload Heapを書き換えないよう、
		// MaterialPass単位で独立した可変strideバッファを保持する

		//--------- variables ----------------------------------------------------

		// ビューバッファ
		MeshBatchViewResources viewResources_{};
		// バッファ
		DefaultStructuredInstanceBuffer<MeshInstanceData> meshData_{ "gMeshInstances" };
		// ExecuteIndirect/AmplificationShaderのカリング結果を書き戻す可視インスタンスバッファ
		StructuredRWBuffer<MeshInstanceData> visibleMeshData_{ "gVisibleMeshInstances" };
		// 同一フレーム内の複数パスで上書きしないper-draw定数領域

		DefaultStructuredInstanceBuffer<MeshSubMeshShaderData> subMeshData_{ "gSubMeshes" };
		// 背面法アウトラインのインスタンス別GPUデータ
		DefaultStructuredInstanceBuffer<MeshOutlineGPUData> outlineData_{ "gMeshOutlines" };

		// サブメッシュ単位マテリアルパラメータ用の可変stride構造化バッファ
		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		// UploadBatchDataで集めるインスタンス×サブメッシュ単位の上書きパラメータ
		std::vector<MaterialParameterSet> subMeshParamScratch_{};
		MeshMaterialBuffers materialBuffers_{};
		// 頂点変位Boundsのマテリアル別キャッシュ
		const MaterialAsset* displacementMetricMaterial_ = nullptr;
		uint64_t displacementMetricMaterialHash_ = 0;
		float cachedMaxDisplacement_ = 0.0f;
		ComPtr<ID3D12Resource> indexedIndirectArgs_{};
		// ExecuteIndirect引数バッファの現在状態
		D3D12_RESOURCE_STATES indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;
		// 可視インスタンスバッファの現在状態
		D3D12_RESOURCE_STATES visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;

		// スキニング用バッファ
		std::unique_ptr<MeshSkinningBufferSet> skinning_{};

		// キャッシュへECSのComponentポインターを保持しない
		struct CachedInstance {
			ECSWorld* world = nullptr;
			Entity entity{};
			AssetID material{};
			uint64_t batchKey = 0;
			uint64_t renderRevision = 0;
			uint64_t resetRevision = 0;
			uint64_t colorRevision = 0;
			uint32_t subMeshIndex = kAllMeshSubMeshes;
			uint32_t subMeshGroupIndex = UINT32_MAX;
			MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
			RenderPhase phase = RenderPhase::Opaque;
			BlendMode blend = BlendMode::Normal;
			bool receiveShadows = true;
			bool operator==(const CachedInstance&) const = default;
		};
		std::vector<CachedInstance> cachedInstances_;
		AssetID cachedMesh_{};
		uint32_t cachedMeshGeneration_ = 0;
		uint64_t parameterGeneration_ = 1;
		std::vector<uint64_t> subMeshParamGenerations_;

		// 毎バッチ再利用するデータ
		std::vector<MeshInstanceData> meshScratch_{};
		std::unordered_multimap<MeshEntityLookupKey, uint32_t,
			MeshEntityLookupKeyHash> meshInstanceIndexMap_{};
		std::vector<MeshSubMeshShaderData> subMeshScratch_{};
		std::vector<MeshOutlineGPUData> outlineScratch_{};

		// upload済みアウトラインデータから計算した保守的メトリクス

		OutlineBatchMetrics outlineMetrics_{};

		// スキニング用の毎バッチ再利用するデータ
		std::vector<WellForGPU> paletteScratch_{};

		// スキニングメッシュを持つエンティティの記録
		std::vector<SkinnedEntityRecord> skinnedRecords_{};
		std::unordered_map<MeshEntityLookupKey, uint32_t,
			MeshEntityLookupKeyHash> skinnedVertexOffsetMap_{};

		// インスタンス数
		uint32_t instanceCount_ = 0;
		uint32_t skinnedInstanceCount_ = 0;

		// 初期化状態
		bool initialized_ = false;

		// スキニング処理をディスパッチしたか
		bool skinningDispatched_ = false;
		bool skinningOutputValid_ = false;
		uint64_t currentSkinningPoseHash_ = 0;
		uint64_t dispatchedSkinningPoseHash_ = 0;
		// 出力バッファ再生成を動的BLASへ伝える世代
		uint64_t skinningBufferGeneration_ = 0;
		bool usesFallbackTexture_ = false;

		//--------- functions ----------------------------------------------------

		// 描画対象からCPU転送データを構築する
		void BuildBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh);

		// CPU側のキャッシュ識別情報を取得する
		static CachedInstance MakeCachedInstance(const RenderSceneBatch& batch, const RenderItem& item);
		// 現在のフレームスロットを再利用する前にper-draw定数の切り出し位置を戻す
		// マテリアルとサブメッシュ上書きから最大頂点変位量を求める
		float ResolveMaxDisplacement(const MaterialAsset* material);
	};
} // Engine
