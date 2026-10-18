#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;

	namespace ViewportDecalGizmo {

		// 選択した投影の範囲と方向を表示する
		void Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize);
	}
}
