#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorGameBuildSession.h"

namespace Engine::EditorGameBuildMenu {

	// ビルド要求をメニューに表示する
	void DrawMenu(const EditorPanelContext& context, EditorGameBuildSession& session);
	// ビルド入力と進行状況を表示する
	void DrawPopup(const EditorPanelContext& context, EditorGameBuildSession& session);
}
