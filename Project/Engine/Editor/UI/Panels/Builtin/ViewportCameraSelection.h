#pragma once

namespace Engine {

	struct EditorPanelContext;

	// Scene表示のCameraを選択する
	namespace ViewportCameraSelection {

		// 有効なCameraの候補を表示する
		void DrawPopup(const EditorPanelContext& context);
	} // ViewportCameraSelection
} // Engine
