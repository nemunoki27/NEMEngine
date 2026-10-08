#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>

namespace Engine {

	struct RenderDrawContext;
	struct MeshPreparedBatch;
	//============================================================================
	//	MeshGraphicsBinding class
	//	Mesh描画用のMaterialと共通資源を接続する
	//============================================================================
	class MeshGraphicsBinding {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshGraphicsBinding();
		// Frame内の定数割当を開始する
		void BeginFrame();
		// 共通資源とMaterialを描画用に接続する
		void Bind(const RenderDrawContext& context, const MeshPreparedBatch& prepared, ID3D12GraphicsCommandList6* commandList);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

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
		FrameConstantBufferAllocator constantBufferAllocator_{};
		D3D12_GPU_VIRTUAL_ADDRESS shaderGraphTimeGPUAddress_ = 0;
		// reflection駆動のサブメッシュ単位マテリアルパラメータ構造化バッファのスロット
		PipelineBindingCache::SlotID subMeshMaterialParamSRVSlot_ = PipelineBindingCache::kInvalidSlot;

	};
}
