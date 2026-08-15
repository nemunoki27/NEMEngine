#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Raytracing/RayTracingProfileAsset.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

namespace Engine {

	//============================================================================
	//	RayTracingPass class
	//	Profile内の指定実行位置に属するDXRエフェクトを順に実行する
	//============================================================================
	class RayTracingPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RayTracingPass(const RenderPipelineDeps& deps,
			RayTracingExecutionPoint executionPoint) :
			deps_(deps), executionPoint_(executionPoint) {
		}
		~RayTracingPass() override = default;

		void Execute(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		RenderPathPassKind GetKind() const override;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		const RenderPipelineDeps& deps_;
		RayTracingExecutionPoint executionPoint_ =
			RayTracingExecutionPoint::AfterLighting;
	};
} // Engine
