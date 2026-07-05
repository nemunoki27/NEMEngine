#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/FillMesh/FillMeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>

namespace Engine {

	//============================================================================
	//	FillMeshRenderBackend class
	//	面メッシュを非インスタンシングでGBufferへ描画する
	//============================================================================
	class FillMeshRenderBackend :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FillMeshRenderBackend() {

			viewCBVSlot_ = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
			objectCBVSlot_ = perDrawBindCache_.AddSlot("ObjectConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			materialParamsCBVSlot_ = perDrawBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
			// 選択アウトラインのマスク描画で使うStyle ID、register(b1, space1)
			outlineMaskCBVSlot_ = perDrawBindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 1);
		}
		~FillMeshRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::FillMesh; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		FrameBatchResourcePool<FillMeshBatchResources> resourcePool_;
		// 描画ごとに別のCBV領域を切り出すアロケータ
		PostProcessConstantBufferAllocator constantBufferAllocator_{};

		RegistryAutoBindTable registryAutoBindTable_{};
		PipelineBindingCache perDrawBindCache_{};
		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID objectCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialParamsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outlineMaskCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		MaterialParameterBinder materialParamBinder_{};
	};
} // Engine
