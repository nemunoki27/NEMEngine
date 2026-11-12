#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine {
	struct EditorState;
}

namespace Engine::EditorMeasurementDisplay {

	// メニューを閉じている間もGPU計測行を更新する
	void UpdateHiddenData();
	// 計測値と詳細ツールチップを表示する
	void DrawData();
	// SceneViewの画像内へ計測値を重ねる
	void DrawOverlay(const EditorState& state, const ImVec2& position, const ImVec2& size, float topInset = 0.0f);
}
