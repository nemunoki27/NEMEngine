#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine::EditorIconButton {

	struct Colors {
		ImVec4 normal;
		ImVec4 hovered;
		ImVec4 pressed;
	};

	// View操作とツールの選択色を共有する
	Colors GetColors(bool selected);
	void PushColors(bool selected);
	void PopColors();

	// アイコンと補助説明を同じ寸法のボタンで表示する
	bool Draw(const char* id, const char* glyph, const char* tooltip, bool selected = false, float size = 0.0f);
}
