#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	PostProcessMaskedUiPass class
	//	PostProcessMaskedUIフェーズのアイテムをSceneFinalへ描画するパス
	//============================================================================
	class PostProcessMaskedUiPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit PostProcessMaskedUiPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~PostProcessMaskedUiPass() override = default;

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::PostProcessMaskedUI; }
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

