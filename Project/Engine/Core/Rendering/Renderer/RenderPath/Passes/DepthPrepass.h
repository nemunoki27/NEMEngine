#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	DepthPrepass class
	//	不透明キューの深度プリパスでオーバーDraw削減パス
	//============================================================================
	class DepthPrepass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit DepthPrepass(const RenderPipelineDeps& deps);
		~DepthPrepass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::DepthPrepass; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		FrameConstantBufferAllocator constantAllocator_{};
		uint64_t constantAllocatorFrameSerial_ = 0;
		AssetID depthPyramidPipeline_{};
		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID constantsCBVSlot_ =
			PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID sourceDepthSRVSlot_ =
			PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outputDepthUAVSlot_ =
			PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// 深度プリパス対象アイテムを収集する
		std::vector<const RenderItem*> CollectItems(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets) const;
		// 現在Viewの深度から同一フレーム用Hi-Zを生成する
		void BuildDepthPyramid(GraphicsCore& graphicsCore,
			SceneExecutionContext& context);
	};
} // Engine
