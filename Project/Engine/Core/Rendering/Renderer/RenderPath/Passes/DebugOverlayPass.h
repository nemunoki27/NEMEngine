#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>

namespace Engine {

	//============================================================================
	//	DebugOverlayPass class
	//	デバッグライン描画を View に重ね描きするパス (_DEBUG/_DEVELOPBUILD のみ)
	//============================================================================
	class DebugOverlayPass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DebugOverlayPass() = default;
		~DebugOverlayPass() override = default;

		std::string_view GetName() const override { return "DebugOverlay"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	};
} // Engine
