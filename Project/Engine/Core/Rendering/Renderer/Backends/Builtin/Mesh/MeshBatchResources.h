#pragma once

//============================================================================
//	include
//============================================================================
#include "MeshMaterialBuffers.h"
#include "MeshBatchIdentityCache.h"
#include "MeshBatchTypes.h"
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
#include <Engine/Core/World/ECS/Entity/WorldEntityKey.h>

// c++
#include <array>
#include <memory>
#include <vector>
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	// 型の前方宣言
	class GraphicsCore;
	class ECSWorld;
	class ECSWorldLifetime;
	class SRVDescriptor;
	struct RenderDrawContext;
	struct MeshGPUResource;
	struct MeshRendererComponent;

	//============================================================================
	//	MeshBatchResources class
	//	Meshのバッチ構築と描画資源を管理
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
		void UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView,
			const ResolvedRenderView* lodView = nullptr);
		// 描画対象のCPUデータを構築して転送
		void UploadBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh);
		// 順序付きの描画構成とEntityの更新世代を照合する
		bool MatchesBatch(const RenderSceneBatch& batch, std::span<const RenderItem* const> items,
			const MeshGPUResource& gpuMesh) const;
		// 色だけが変わったインスタンスのパラメータを更新する
		uint32_t RefreshMaterialColors();
		// 構成再抽出後も一致するバッチは行列だけ同期する
		void RefreshBatchTransforms(std::span<const RenderItem* const> items);
		// 現在のFrameへ未反映の範囲を転送
		void UploadCachedBatchData();
		// 描画ごとにPassの定数を更新
		void UpdateDrawConstants(const RenderDrawContext& drawContext,
			const MeshGPUResource& gpuMesh, uint32_t subMeshIndex,
			uint32_t subMeshGroupIndex, const MaterialAsset* material, bool normalConeAllowed);
		// 間接描画のIndex数を更新
		void UpdateIndexedIndirectArgsConstants(uint32_t indexCount);

		// Shaderの配置に合わせてサブメッシュのMaterial値を転送
		void UploadSubMeshMaterialParams(const MaterialAsset* material,
			const MaterialParameterLayout& layout, const RenderDrawContext& drawContext);

		// スキニングに使用するリソースを確保する
		void EnsureSkinningResources(GraphicsCore& graphicsCore);
		// 現在のポーズに対するスキニング処理完了を記録する
		void MarkSkinningDispatched(uint64_t pipelineID);
		// 再計算に必要なパレットと定数を転送する
		void UploadSkinningInputs(const MeshGPUResource& gpuMesh);

		//--------- accessor -----------------------------------------------------

		// スキニングするインスタンスの頂点オフセットを取得する
		bool FindSkinnedVertexOffset(const ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const;

		// 同じポーズとパイプラインの計算結果を再利用する
		bool CanReuseSkinningOutput(uint64_t pipelineID) const;
		// 未計算の頂点を使わず元の形状へ戻す
		void SetSkinningAvailable(bool available);
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
		// カリング後のInstance配列を取得
		D3D12_GPU_VIRTUAL_ADDRESS GetVisibleInstanceMeshGPUAddress() const { return visibleMeshData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return viewResources_.GetDrawGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndirectArgsConstantsGPUAddress() const { return viewResources_.GetIndirectGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshGPUAddress() const { return subMeshData_.GetGPUAddress(); }
		// Shaderの配置に対応するMaterialバッファ
		bool HasSubMeshMaterialParams() const { return materialBuffers_.IsAvailable(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSubMeshMaterialParamGPUAddress() const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetSubMeshMaterialParamGPUHandle() const;
		std::string_view GetSubMeshMaterialParamBindingName() const { return MaterialParameterCBuffer::kMesh; }
		// 背面法アウトラインのインスタンス別GPUデータ
		D3D12_GPU_VIRTUAL_ADDRESS GetOutlineGPUAddress() const { return outlineData_.GetGPUAddress(); }
		std::string_view GetOutlineBindingName() const { return outlineData_.GetBindingName(); }
		// 輪郭MaskのStyleと対象サブメッシュを取得
		D3D12_GPU_VIRTUAL_ADDRESS GetScreenSpaceOutlineMaskGPUAddress() const { return viewResources_.GetMaskGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningPaletteGPUAddress() const { return skinning_->skinningPalette.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinningConstantsGPUAddress() const { return skinning_->skinningConstants.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetSkinnedVerticesGPUAddress() const { return skinning_->skinnedVertices.GetGPUAddress(); }
		// Skinning済みの圧縮頂点を取得
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

		// ShaderのBinding名を取得
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
		uint64_t GetSkinningResultGeneration() const { return skinningResultGeneration_; }

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

		// WorldとEntityの共通検索キー
		using MeshEntityLookupKey = WorldEntityKey;
		using MeshEntityLookupKeyHash = WorldEntityKeyHash;

		//--------- variables ----------------------------------------------------

		// ビューバッファ
		MeshBatchViewResources viewResources_{};
		// バッファ
		DefaultStructuredInstanceBuffer<MeshInstanceData> meshData_{ "gMeshInstances" };
		// カリングで残ったインスタンスを書き戻すバッファ
		StructuredRWBuffer<MeshInstanceData> visibleMeshData_{ "gVisibleMeshInstances" };
		DefaultStructuredInstanceBuffer<MeshSubMeshShaderData> subMeshData_{ "gSubMeshes" };
		// 背面法アウトラインのインスタンス別GPUデータ
		DefaultStructuredInstanceBuffer<MeshOutlineGPUData> outlineData_{ "gMeshOutlines" };

		// Material転送に使用するDeviceとDescriptor
		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		// InstanceとSubMeshごとのMaterial上書き
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

		// CPU側の構成とWorldの寿命を保持
		MeshBatchIdentityCache batchIdentity_{};
		uint64_t parameterGeneration_ = 1;
		std::vector<uint64_t> subMeshParamGenerations_;

		// 毎バッチ再利用するデータ
		std::vector<MeshInstanceData> meshScratch_{};
		std::unordered_multimap<MeshEntityLookupKey, uint32_t,
			MeshEntityLookupKeyHash> meshInstanceIndexMap_{};
		std::vector<MeshSubMeshShaderData> subMeshScratch_{};
		std::vector<MeshOutlineGPUData> outlineScratch_{};

		// 転送済みの輪郭を包む描画範囲
		OutlineBatchMetrics outlineMetrics_{};

		// スキニング用の毎バッチ再利用するデータ
		std::vector<WellForGPU> paletteScratch_{};

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
		uint64_t dispatchedSkinningPipelineID_ = 0;
		uint64_t skinningResultGeneration_ = 0;
		bool skinningDrawEnabled_ = true;
		// Skinningの再生成をBLASへ伝える世代
		uint64_t skinningBufferGeneration_ = 0;
		bool usesFallbackTexture_ = false;

		//--------- functions ----------------------------------------------------

		// 描画対象からCPU転送データを構築する
		void BuildBatchData(const RenderDrawContext& drawContext, const RenderSceneBatch& batch,
			std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh);

		// マテリアルとサブメッシュ上書きから最大頂点変位量を求める
		float ResolveMaxDisplacement(const MaterialAsset* material);
	};
}
