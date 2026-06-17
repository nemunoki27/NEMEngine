#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

namespace Engine {

	//============================================================================
	//	TransparentRenderPass class
	//	TransparentキューをSceneFinalに描画するパス
	//============================================================================
	class TransparentRenderPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit TransparentRenderPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~TransparentRenderPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::Transparent; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
	};
} // Engine

