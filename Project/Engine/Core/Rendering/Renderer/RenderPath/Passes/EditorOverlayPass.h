#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>

namespace Engine {

	//============================================================================
	//	EditorOverlayPass class
	//	エディタ専用のオーバーレイを View に重ね描きするパス
	//============================================================================
	class EditorOverlayPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		EditorOverlayPass() = default;
		~EditorOverlayPass() override = default;

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::EditorOverlay; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	};
} // Engine
