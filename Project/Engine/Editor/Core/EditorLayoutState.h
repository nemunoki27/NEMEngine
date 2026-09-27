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
		// trueの間はMenuBarを含む全エディターUIを描画せず、GameViewを直接表示する
		bool hidePanels = false;
		// Play開始時にManagedデバッガのアタッチ待機を行う
		bool waitForManagedDebuggerOnPlay = false;
		// Play開始前に編集中のSceneを保存する
		bool autoSaveScenesOnPlay = true;

		// 各ウィンドウのサイズ
		ImVec2 lastSceneViewSize = ImVec2(0.0f, 0.0f);
		ImVec2 lastGameViewSize = ImVec2(0.0f, 0.0f);
	};
} // Engine
