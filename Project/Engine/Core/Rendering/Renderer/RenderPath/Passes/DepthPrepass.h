#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

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

		explicit DepthPrepass(const RenderPipelineDeps& deps) : deps_(deps) {}
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

		//--------- functions ----------------------------------------------------

		// 深度プリパス対象アイテムを収集する
		std::vector<const RenderItem*> CollectItems(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets) const;
	};
} // Engine
