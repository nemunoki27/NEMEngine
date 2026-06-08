#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayCollector.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayRegistry.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayRenderer.h>

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
	private:
		SceneComponentOverlaySettings settings_{};
		SceneComponentOverlayRegistry registry_{};
		SceneComponentOverlayCollector collector_{};
		SceneComponentOverlayRenderer renderer_{};
		SceneComponentOverlayItemList items_{};
	};
} // Engine
