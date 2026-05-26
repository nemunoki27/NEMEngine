#pragma once

//============================================================================
//	include
//============================================================================

#include <imgui.h>

namespace Engine {

	//============================================================================
	//	EditorLayoutState struct
	//============================================================================

	// エディタのUIレイアウト状態
	struct EditorLayoutState {

		// UIの表示状態
		bool showHierarchy = true;
		bool showInspector = true;
		bool showProject = true;
		bool showConsole = true;
		bool showSceneView = true;
		bool showGameView = true;
		bool showToolbar = true;
		bool showTool = true;
		// Play開始時にManagedデバッガのアタッチ待機を行う
		bool waitForManagedDebuggerOnPlay = false;

		// 各ウィンドウのサイズ
		ImVec2 lastSceneViewSize = ImVec2(0.0f, 0.0f);
		ImVec2 lastGameViewSize = ImVec2(0.0f, 0.0f);
	};
} // Engine
