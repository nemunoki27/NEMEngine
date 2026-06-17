#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>

namespace Engine {

	//============================================================================
	//	DebugOverlayPass class
	//	デバッグライン描画をViewに重ね描きするパス(_DEBUG/_DEVELOPBUILDのみ)
	//============================================================================
	class DebugOverlayPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DebugOverlayPass() = default;
		~DebugOverlayPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::DebugOverlay; }
	};
} // Engine
