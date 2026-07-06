#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/FillMesh/FillMeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>

namespace Engine {

	//============================================================================
	//	FillMeshRenderBackend class
	//	面メッシュを非インスタンシングでGBufferへ描画する
	//============================================================================
	class FillMeshRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FillMeshRenderBackend() {

			objectCBVSlot_ = perDrawBindCache_.AddSlot("ObjectConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			// 選択アウトラインのマスク描画で使うStyle ID
			outlineMaskCBVSlot_ = perDrawBindCache_.AddSlotByRegister(ShaderBindingKind::CBV,
				kScreenSpaceOutlineMaskCBVRegister, kScreenSpaceOutlineMaskCBVSpace);
		}
		~FillMeshRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		// 非インスタンシングなので常に単独描画にする
		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::FillMesh; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		FrameBatchResourcePool<FillMeshBatchResources> resourcePool_;

		PipelineBindingCache::SlotID objectCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outlineMaskCBVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine
