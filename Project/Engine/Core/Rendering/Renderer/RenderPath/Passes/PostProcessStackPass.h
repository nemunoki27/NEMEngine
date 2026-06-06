#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	PostProcessStackPass class
	//	SceneFinal にポストプロセスエフェクトを適用するパス
	//============================================================================
	class PostProcessStackPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit PostProcessStackPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~PostProcessStackPass() override = default;

		std::string_view GetName() const override { return "PostProcessStack"; }
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
