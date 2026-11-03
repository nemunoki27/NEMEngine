#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;

	// 選択した移動アニメーションの経路をScene画像へ描く
	class ViewportMotionGizmo {
	public:

		static void Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize);
	};
}
