#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/ComPtr.h>

namespace Engine {

	//============================================================================
	//	VertexMeshDrawPath class
	//	頂点シェーダー描画パス
	//============================================================================
	class VertexMeshDrawPath :
		public IMeshDrawPath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		VertexMeshDrawPath();
		~VertexMeshDrawPath() = default;

		bool Supports(const PipelineVariantDesc& variant) const override;

		void Setup(const MeshPathSetupContext& context) override;

		void Draw(const MeshPathDrawContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ExecuteIndirectでDrawIndexedInstancedを1回発行するためのシグネチャ
		ComPtr<ID3D12CommandSignature> commandSignature_{};
		// 可視インスタンスとIndirectArgsを生成するComputeパイプライン
		AssetID indirectArgsPipeline_{};

		// BuildIndexedIndirectArgs用Computeバインドのキャッシュ
		PipelineBindingCache indirectArgsBindCache_;
		PipelineBindingCache::SlotID indirectArgsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID iaViewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID iaDrawCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID iaMeshInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID iaSubMeshSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID visibleInstUAVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID idxIndirectArgsUAVSlot_ = PipelineBindingCache::kInvalidSlot;

		// Draw時の可視インスタンスSRV再バインド用
		PipelineBindingCache drawBindCache_;
		PipelineBindingCache::SlotID drawMeshInstSRVSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		void EnsureCommandSignature(ID3D12Device* device);
		// カリング結果を反映したDrawIndexedIndirect引数をGPU上で作成する
		bool BuildIndexedIndirectArgs(const MeshPathDrawContext& context);
	};
} // Engine
