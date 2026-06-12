#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	PostProcessStackPass class
	//	SceneFinalにポストプロセスエフェクトを適用するパス
	//============================================================================
	class PostProcessStackPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit PostProcessStackPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~PostProcessStackPass() override = default;

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::PostProcessStack; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
	};
} // Engine

