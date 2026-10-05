#include "ImGuiAppearance.h"

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

	const char* fontPath = "C:\\Windows\\Fonts\\meiryob.ttc";
	if (std::filesystem::exists(fontPath)) {
		io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());
	} else {
		// 日本語フォントがなければ既定フォントを使う
		io.Fonts->AddFontDefault();
	}
}

void Engine::ImGuiAppearance::ApplyTheme(ImGuiStyle& style, bool platformWindows) {

	ImVec4* colors = style.Colors;

	auto C = [](int r, int g, int b, int a = 255) -> ImVec4 { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); };

	// 背景と境界の色を揃える
	const ImVec4 bg0 = C(0, 0, 0);			  // WindowBg
	const ImVec4 topbar = C(3, 3, 3);		  // Title/Menu
	const ImVec4 panel = C(6, 6, 6);		  // Frame/Button/Header
	const ImVec4 border = C(28, 28, 28, 130); // Border

	// 選択状態の色を揃える
	const ImVec4 accentH = C(18, 62, 150, 230);
	const ImVec4 accentA = C(26, 82, 190, 255);
	const ImVec4 accentLo = C(12, 45, 115, 70);

	// 文字色を設定する
	colors[ImGuiCol_Text] = C(125, 125, 125);
	colors[ImGuiCol_TextDisabled] = C(78, 78, 78, 153);

	// WindowとPopupの背景を設定する
	colors[ImGuiCol_WindowBg] = bg0;
	colors[ImGuiCol_ChildBg] = bg0;
	colors[ImGuiCol_PopupBg] = C(2, 2, 2, 245);

	// 境界線の色を設定する
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_BorderShadow] = C(0, 0, 0, 0);

	// 入力欄の色を設定する
	colors[ImGuiCol_FrameBg] = C(5, 5, 5);
	colors[ImGuiCol_FrameBgHovered] = C(10, 10, 10);
	colors[ImGuiCol_FrameBgActive] = C(0, 13, 85, 255);

	// タイトルとメニューの背景を設定する
	colors[ImGuiCol_TitleBg] = topbar;
	colors[ImGuiCol_TitleBgActive] = C(5, 5, 5);
	colors[ImGuiCol_TitleBgCollapsed] = C(0, 0, 0);
	colors[ImGuiCol_MenuBarBg] = C(4, 4, 4);

	// スクロールバーの色を設定する
	colors[ImGuiCol_ScrollbarBg] = bg0;
	colors[ImGuiCol_ScrollbarGrab] = C(12, 12, 12);
	colors[ImGuiCol_ScrollbarGrabHovered] = C(20, 20, 20);
	colors[ImGuiCol_ScrollbarGrabActive] = C(32, 32, 32);

	// チェックとスライダーの色を設定する
	colors[ImGuiCol_CheckMark] = C(0, 40, 255, 255);
	colors[ImGuiCol_CheckboxSelectedBg] = C(7, 7, 7, 255);
	colors[ImGuiCol_SliderGrab] = C(120, 120, 120, 150);
	colors[ImGuiCol_SliderGrabActive] = accentA;

	// ボタンの操作状態ごとの色を設定する
	colors[ImGuiCol_Button] = panel;
	colors[ImGuiCol_ButtonHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_ButtonActive] = C(0, 7, 45, 255);

	// 見出しの操作状態ごとの色を設定する
	colors[ImGuiCol_Header] = C(13, 13, 13, 255);
	colors[ImGuiCol_HeaderHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_HeaderActive] = C(0, 7, 45, 255);

	// 区切り線とリサイズ部分の色を設定する
	colors[ImGuiCol_Separator] = border;
	colors[ImGuiCol_SeparatorHovered] = accentH;
	colors[ImGuiCol_SeparatorActive] = accentA;

	colors[ImGuiCol_ResizeGrip] = accentLo;
	colors[ImGuiCol_ResizeGripHovered] = C(18, 62, 150, 145);
	colors[ImGuiCol_ResizeGripActive] = C(0, 7, 45, 255);

	// タブの操作状態ごとの色を設定する
	colors[ImGuiCol_Tab] = topbar;
	colors[ImGuiCol_TabHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_TabSelected] = C(8, 8, 8);
	colors[ImGuiCol_TabSelectedOverline] = accentA;
	colors[ImGuiCol_TabDimmed] = C(1, 1, 1);
	colors[ImGuiCol_TabDimmedSelected] = C(5, 5, 5);
	colors[ImGuiCol_TabDimmedSelectedOverline] = C(18, 62, 150, 145);

	// ドッキング先の色を設定する
	colors[ImGuiCol_DockingPreview] = C(12, 45, 115, 65);
	colors[ImGuiCol_DockingEmptyBg] = bg0;

	// グラフの色を設定する
	colors[ImGuiCol_PlotLines] = C(130, 130, 130);
	colors[ImGuiCol_PlotLinesHovered] = accentA;
	colors[ImGuiCol_PlotHistogram] = C(130, 130, 130);
	colors[ImGuiCol_PlotHistogramHovered] = accentA;

	// 選択範囲と配置先の色を設定する
	colors[ImGuiCol_TextSelectedBg] = accentLo;
	colors[ImGuiCol_DragDropTarget] = C(0, 15, 98);

	// 操作対象とModal背景の色を設定する
	colors[ImGuiCol_NavHighlight] = accentA;
	colors[ImGuiCol_NavWindowingHighlight] = C(18, 62, 150, 170);
	colors[ImGuiCol_NavWindowingDimBg] = C(0, 0, 0, 180);
	colors[ImGuiCol_ModalWindowDimBg] = C(0, 0, 0, 205);

	colors[ImGuiCol_TableBorderStrong] = C(31, 31, 31);
	colors[ImGuiCol_TableBorderLight] = C(31, 31, 31);
	colors[ImGuiCol_TableRowBgAlt] = C(4, 4, 4);
	colors[ImGuiCol_TableHeaderBg] = C(2, 2, 2);

	// 角丸と余白を設定する
	style.WindowRounding = 2.0f;
	style.ChildRounding = 2.0f;
	style.FrameRounding = 2.0f;
	style.FramePadding = ImVec2(2.0f, 2.0f);
	style.ScrollbarRounding = 2.0f;
	style.GrabRounding = 2.0f;
	style.TabRounding = 2.0f;

	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;

	style.DockingSeparatorSize = 2.0f;

	// 外部Windowの枠と背景を揃える
	if (platformWindows) {
		style.WindowRounding = 0.0f;
		colors[ImGuiCol_WindowBg].w = 1.0f;
	}
}
