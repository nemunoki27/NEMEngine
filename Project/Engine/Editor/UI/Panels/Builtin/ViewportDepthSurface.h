#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Debug/DepthVisualizer.h>

namespace Engine {

	struct GizmoViewportRect;

	//============================================================================
	//	ViewportDepthSurface class
	//	深度表示の描画資源を所有する
	//============================================================================
	class ViewportDepthSurface {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// GBufferデバッグのDepth表示で、ビューの深度を可視化サーフェスへ描いて表示用テクスチャを返す
		const RenderTexture2D* RenderDepthVisualization(const EditorPanelContext& context,
			RenderViewKind viewKind, uint32_t width, uint32_t height);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// GBufferデバッグのDepth表示用、深度を線形化グレースケールへ変換して表示する
		DepthVisualizer depthVisualizer_{};
		std::unique_ptr<MultiRenderTarget> depthVisualizeSurface_;
		uint32_t depthVisualizeWidth_ = 0;
		uint32_t depthVisualizeHeight_ = 0;

		//--------- functions ----------------------------------------------------

	};
}
