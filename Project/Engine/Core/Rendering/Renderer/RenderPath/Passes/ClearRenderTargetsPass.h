#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

namespace Engine {

	//============================================================================
	//	ClearRenderTargetsPass class
	//	SceneMain / SceneFinalをクリアするパス
	//============================================================================
	class ClearRenderTargetsPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit ClearRenderTargetsPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~ClearRenderTargetsPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::ClearRenderTargets; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
	};
} // Engine

