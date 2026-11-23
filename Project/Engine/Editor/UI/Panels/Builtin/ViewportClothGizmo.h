#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;

	// 選択した布の固定点をScene画像へ描く
	class ViewportClothGizmo {
	public:

		static void Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize);
	};
}
