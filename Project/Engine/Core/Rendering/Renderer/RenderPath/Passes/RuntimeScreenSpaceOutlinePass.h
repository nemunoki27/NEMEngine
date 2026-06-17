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
	//	RuntimeScreenSpaceOutlinePass class
	//スクリーンスペースアウトラインの合成
	//============================================================================
	class RuntimeScreenSpaceOutlinePass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit RuntimeScreenSpaceOutlinePass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~RuntimeScreenSpaceOutlinePass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RuntimeScreenSpaceOutline; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		ScreenSpaceOutlineRenderer renderer_{};
		std::vector<ScreenSpaceOutlineRequest> requests_{};

		//--------- functions ----------------------------------------------------

		void CollectRequests(const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets);
	};
} // Engine

