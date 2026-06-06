#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	TransparentRenderPass class
	//	Transparentキューを SceneFinal に描画するパス
	//============================================================================
	class TransparentRenderPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit TransparentRenderPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~TransparentRenderPass() override = default;

		std::string_view GetName() const override { return "Transparent"; }
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
