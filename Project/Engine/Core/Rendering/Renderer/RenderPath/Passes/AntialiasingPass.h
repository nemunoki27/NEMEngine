#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

namespace Engine {

	struct GraphicsFeaturePreferences;

	//============================================================================
	//	AntialiasingPass class
	//	出力解像度のFXAAをトーンマッピング後に適用する
	//============================================================================
	class AntialiasingPass : public IRenderPass {
	public:
		explicit AntialiasingPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		static bool CanRenderToView(const GraphicsFeaturePreferences& preferences, const SceneExecutionContext& context);
		// トーンマッピングの出力をFXAAの入力として確保する
		static MultiRenderTarget* PrepareToneMapTarget(GraphicsCore& graphicsCore, SceneExecutionContext& context,
			const RenderPipelineDeps& deps);
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;
		RenderPathPassKind GetKind() const override { return RenderPathPassKind::Antialiasing; }
	private:
		RenderPipelineDeps deps_{};
	};
}
