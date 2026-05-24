#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	ScreenUiPass class
	//	ScreenUIフェーズのアイテムをViewport表示後のViewへ描画するパス
	//============================================================================
	class ScreenUiPass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ScreenUiPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~ScreenUiPass() override = default;

		std::string_view GetName() const override { return "ScreenUI"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
	};
} // Engine
