#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineRenderer.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	EditorSelectionScreenSpaceOutlinePass class
	// Editor選択由来のtemporary requestをSceneFinalへ合成する
	// PostProcess後、Blit前にSceneViewだけへ描く
	//============================================================================
	class EditorSelectionScreenSpaceOutlinePass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit EditorSelectionScreenSpaceOutlinePass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~EditorSelectionScreenSpaceOutlinePass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::EditorSelectionScreenSpaceOutline; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		ScreenSpaceOutlineRenderer renderer_{};
		std::vector<ScreenSpaceOutlineRequest> requests_{};
	};
} // Engine

