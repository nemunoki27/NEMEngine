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
	//	エディタ専用のオーバーレイをViewに重ね描きするパス
	//============================================================================
	class EditorOverlayPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EditorOverlayPass() = default;
		~EditorOverlayPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::EditorOverlay; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		SceneComponentOverlaySettings settings_{};
		SceneComponentOverlayRegistry registry_{};
		SceneComponentOverlayCollector collector_{};
		SceneComponentOverlayRenderer renderer_{};
		SceneComponentOverlayItemList items_{};
	};
} // Engine
