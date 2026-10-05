#pragma once

struct ImGuiIO;
struct ImGuiStyle;

namespace Engine::ImGuiAppearance {

	// 日本語フォントを設定する
	void ConfigureFont(ImGuiIO& io);
	// エディターの配色と余白を設定する
	void ApplyTheme(ImGuiStyle& style, bool platformWindows);
}
