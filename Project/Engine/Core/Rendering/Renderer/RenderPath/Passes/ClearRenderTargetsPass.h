#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	ClearRenderTargetsPass class
	//	SceneMain / SceneFinal をクリアするパス
	//============================================================================
	class ClearRenderTargetsPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit ClearRenderTargetsPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~ClearRenderTargetsPass() override = default;

		std::string_view GetName() const override { return "ClearRenderTargets"; }
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

