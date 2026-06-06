#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineRenderer.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	RuntimeScreenSpaceOutlinePass class
	//	ScreenSpaceOutlineComponent由来のoutlineをSceneFinalへ合成する。
	//	PostProcess前に実行し、GameView/SceneViewの両方で表示する。
	//============================================================================
	class RuntimeScreenSpaceOutlinePass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit RuntimeScreenSpaceOutlinePass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~RuntimeScreenSpaceOutlinePass() override = default;

		std::string_view GetName() const override { return "RuntimeScreenSpaceOutline"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		ScreenSpaceOutlineRenderer renderer_{};
		std::vector<ScreenSpaceOutlineRequest> requests_{};

		//--------- functions ----------------------------------------------------

		void CollectRequests(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets);
	};
} // Engine
