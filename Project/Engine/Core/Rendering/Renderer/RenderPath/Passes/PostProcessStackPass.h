#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessAnchor.h>

// c++
#include <string>

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

		PostProcessStackPass(const RenderPipelineDeps& deps, PostProcessAnchor anchor) : deps_(deps), anchor_(anchor) {}
		~PostProcessStackPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::PostProcessStack; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		// このパスインスタンスが担当する挿入位置、ここに割り当てられたポストだけを実行する
		PostProcessAnchor anchor_;
		std::string lastGraphDiagnostic_{};
	};
} // Engine

