#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveBatchResources.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>

namespace Engine {

	//============================================================================
	//	PrimitiveRenderBackend class
	//	プロシージャル形状を共有ジオメトリでインスタンシング描画する、不透明はGBuffer半透明はフォワード
	//============================================================================
	class PrimitiveRenderBackend :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrimitiveRenderBackend() {

			viewCBVSlot_ = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
			meshConstantsCBVSlot_ = perDrawBindCache_.AddSlot("PrimitiveMeshConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			instancesSRVSlot_ = perDrawBindCache_.AddSlot("gInstances", ShaderBindingKind::SRV);
			indicesSRVSlot_ = perDrawBindCache_.AddSlot("gIndices", ShaderBindingKind::SRV);
			materialParamsCBVSlot_ = perDrawBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
			// 選択アウトラインのマスク描画で使うStyle ID、register(b1, space1)
			outlineMaskCBVSlot_ = perDrawBindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 1);
		}
		~PrimitiveRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Primitive; }
		// レイトレ側から共有ジオメトリを参照するため公開する
		PrimitiveGeometryManager& GetGeometryManager() { return geometryManager_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 形状ハッシュ単位で共有するジオメトリ
		PrimitiveGeometryManager geometryManager_{};
		bool geometryManagerInitialized_ = false;

		FrameBatchResourcePool<PrimitiveBatchResources> resourcePool_;
		// 描画ごとに別のCBV領域を切り出すアロケータ
		PostProcessConstantBufferAllocator constantBufferAllocator_{};

		RegistryAutoBindTable registryAutoBindTable_{};
		PipelineBindingCache perDrawBindCache_{};
		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID instancesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID indicesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialParamsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outlineMaskCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		MaterialParameterBinder materialParamBinder_{};

		//--------- functions ----------------------------------------------------

		// バッチのインスタンスデータを集める
		void CollectInstances(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			std::vector<PrimitiveInstanceData>& outInstances) const;
	};
} // Engine
