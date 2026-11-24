#pragma once

//============================================================================
//	include
//============================================================================
#include <imgui.h>

namespace Engine::EditorIconButton {

	namespace Playback {
		inline constexpr char Play[] = "\xef\x81\x8b";
		inline constexpr char Pause[] = "\xef\x81\x8c";
		inline constexpr char Stop[] = "\xef\x81\x8d";
		inline constexpr char Previous[] = "\xef\x81\x93";
		inline constexpr char Next[] = "\xef\x81\x94";
		inline constexpr char First[] = "\xef\x81\x88";
		inline constexpr char Last[] = "\xef\x81\x91";
		inline constexpr char Resume[] = "\xef\x81\x8e";
		inline constexpr char Clear[] = "\xef\x8b\xad";
		inline constexpr char Reset[] = "\xef\x8b\xaa";
		inline constexpr char EmitOnce[] = "\xef\x83\xa7";
	}

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
	bool Draw(const char* id, const char* glyph, const char* tooltip, bool selected, const ImVec2& size);
	// テクスチャのアイコンも同じ選択色と寸法で表示する
	bool DrawImage(const char* id, ImTextureID texture, const char* tooltip, bool selected, float size);
	bool DrawImage(const char* id, ImTextureID texture, const char* tooltip, bool selected, const ImVec2& size);
}
