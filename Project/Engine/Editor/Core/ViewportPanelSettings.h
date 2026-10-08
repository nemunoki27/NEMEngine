#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorState.h"

namespace Engine::ViewportPanelSettings {

	// 表示と操作の設定を読み込む
	void Load(EditorState& state);
	// 表示と操作の設定を保存する
	void Save(const EditorState& state);
}
