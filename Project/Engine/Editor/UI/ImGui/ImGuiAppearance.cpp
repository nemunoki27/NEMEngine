#include "ImGuiAppearance.h"
#include "EditorPalette.h"
#include "EditorWindowPresentation.h"

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

// imgui
#include <imgui.h>

void Engine::ImGuiAppearance::ConfigureFont(ImGuiIO& io) {

	// ImGuiのフォント設定
	ImFontConfig cfg{};
	cfg.FontNo = 0;
	static constexpr ImWchar iconRange[] = { 0xe29c, 0xe29c, 0xf000, 0xf8ff, 0 };
	cfg.GlyphExcludeRanges = iconRange;

	const char* fontPath = "C:\\Windows\\Fonts\\meiryob.ttc";
	if (std::filesystem::exists(fontPath)) {
		io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());
	} else {
		// 日本語フォントがなければ既定フォントを使う
		io.FontDefault = io.Fonts->AddFontDefault(&cfg);
	}
	EditorWindowPresentation::ConfigureFont(io);
}

void Engine::ImGuiAppearance::ApplyTheme(ImGuiStyle& style, bool platformWindows) {

	EditorPalette::Apply(style, platformWindows);
}