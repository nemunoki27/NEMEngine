#include "EditorWindowTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorWindowPresentation.h>
#include <Engine/Editor/UI/ImGui/ImGuiAppearance.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <cmath>
#include <iostream>
#include <string_view>
// imgui
#include <imgui_internal.h>

bool NEMTests::TestEditorWindows() {

	using namespace Engine;
	using namespace EditorWindowPresentation;
	ImGui::CreateContext();
	const ScopedCleanup cleanup([]() noexcept { ImGui::DestroyContext(); });
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.DeltaTime = 1.0f / 60.0f;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.FontDefault = io.Fonts->AddFontDefault();
	if (HasIconFont() || MakeWindowName("Hierarchy", "Hierarchy", "Hierarchy") != "Hierarchy###Hierarchy") return false;
	io.Fonts->Clear();
	ImGuiAppearance::ConfigureFont(io);
	ImGuiAppearance::ApplyTheme(ImGui::GetStyle(), false);
	if (!HasIconFont()) return false;

	constexpr const char* keys[] = {
		"SceneView", "GameView", "Hierarchy", "Inspector", "Project", "Console", "Toolbar",
		"engine.sceneViewCamera", "engine.collision_manager", "engine.animation_clip", "engine.render_features",
		"engine.material_editor", "engine.shader_graph", "engine.tag_manager",
		"engine.performance_check", "engine.script_execution_order", "engine.script_profiler", "engine.input_device",
		"engine.script_build_diagnostics", "engine.script_exception_list",
	};
	ImGui::NewFrame();
	for (const char* key : keys) {
		const auto name = MakeWindowName(key, key, key);
		if (ImHashStr(name.c_str()) != ImHashStr(key) || std::string_view(GetIcon(key)) == GetIcon("unknown")) return false;
		if (std::string_view(GetPlainTitle(name.c_str())) != std::string(key) + "###" + key) return false;
		unsigned int codepoint = 0;
		ImTextCharFromUtf8(&codepoint, GetIcon(key), nullptr);
		for (float size : {14.0f, 21.0f, 28.0f}) {
			const auto* glyph = io.FontDefault->GetFontBaked(size)->FindGlyphNoFallback(static_cast<ImWchar>(codepoint));
			if (!glyph || !glyph->Visible) {
				std::cerr << "Missing window icon: " << key << '\n';
				return false;
			}
			if (std::string_view(key) == "SceneView" || std::string_view(key) == "GameView") {
				const auto* letter = io.FontDefault->GetFontBaked(size)->FindGlyphNoFallback('H');
				if (std::abs(glyph->Y0 + glyph->Y1 - letter->Y0 - letter->Y1) > size * 0.22f) {
					std::cerr << "Window icon alignment: " << key << '\n';
					return false;
				}
			}
		}
	}
	if (std::string_view(GetPlainTitle("通常のタイトル")) != "通常のタイトル") return false;
	const auto first = MakeWindowName("Inspector: First", "Inspector:inspector.primary", "Inspector");
	const auto renamed = MakeWindowName("Inspector: 名前変更", "Inspector:inspector.primary", "Inspector");
	const auto duplicate = MakeWindowName("Inspector: First", "Inspector:duplicate", "Inspector");
	if (ImHashStr(first.c_str()) != ImHashStr(renamed.c_str()) ||
		ImHashStr(first.c_str()) == ImHashStr(duplicate.c_str()) ||
		ImHashStr(first.c_str()) != ImHashStr("Inspector###Inspector:inspector.primary")) return false;

	const auto& style = ImGui::GetStyle();
	if (style.WindowMenuButtonPosition != ImGuiDir_None || style.DockingNodeHasCloseButton ||
		style.TabRounding != 0.0f || style.TabBarOverlineSize != 0.0f) return false;
	const float bodyFont = ImGui::GetFontSize();
	const ImVec2 bodyPadding = style.FramePadding;
	const ImVec4 bodyColor = style.Colors[ImGuiCol_Text];
	bool open = true;
	ImGui::SetNextWindowPos(ImVec2(100, 100));
	ImGui::SetNextWindowSize(ImVec2(400, 300));
	Begin(first.c_str(), &open);
	const bool restored = ImGui::GetFontSize() == bodyFont && style.FramePadding.x == bodyPadding.x &&
		style.FramePadding.y == bodyPadding.y && style.Colors[ImGuiCol_Text].x == bodyColor.x;
	ImGui::TextUnformatted("パネル内の表示");
	ImGui::End();
	Begin("New tool###new-tool", &open);
	const auto initialSize = ImGui::GetWindowSize();
	ImGui::End();
	ImGui::Render();
	if (!restored || !open || initialSize.x != 600.0f || initialSize.y != 450.0f) return false;
	// タイトルをドラッグしても共通の外観が移動操作を妨げないことを確認する
	for (int frame = 0; frame < 4; ++frame) {
		io.AddMousePosEvent(frame < 2 ? 150.0f : 350.0f, frame < 2 ? 110.0f : 210.0f);
		if (frame == 1) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		if (frame == 3) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		ImGui::NewFrame();
		Begin(first.c_str(), &open);
		ImGui::TextUnformatted("パネル内の表示");
		ImGui::End();
		ImGui::Render();
	}
	const auto* moved = ImGui::FindWindowByName(first.c_str());
	if (!moved || moved->Pos.x < 250.0f || moved->Pos.y < 150.0f) {
		std::cerr << "Window title drag failed\n";
		return false;
	}

	ImGui::LoadIniSettingsFromMemory("[Window][Hierarchy]\nPos=50,60\nSize=400,500\nCollapsed=0\nDockId=0x00001111,0\n\n");
	const auto hierarchy = MakeWindowName("Hierarchy", "Hierarchy", "Hierarchy");
	const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(hierarchy.c_str()));
	if (!settings || settings->Pos.x != 50 || settings->Size.y != 500 || settings->DockId != 0x1111) return false;
	const ImGuiID nodeID = ImGui::DockBuilderAddNode(0x1111,
		ImGuiDockNodeFlags_HiddenTabBar | static_cast<int>(ImGuiDockNodeFlags_AutoHideTabBar));
	PrepareDockNodes();
	const auto* node = ImGui::DockBuilderGetNode(nodeID);
	if (!node || node->IsHiddenTabBar() || (node->MergedFlags & ImGuiDockNodeFlags_AutoHideTabBar)) return false;
	ImGui::DockBuilderSetNodePos(nodeID, ImVec2(500, 100));
	ImGui::DockBuilderSetNodeSize(nodeID, ImVec2(600, 500));
	ImGui::DockBuilderDockWindow(first.c_str(), nodeID);
	ImGui::DockBuilderDockWindow(duplicate.c_str(), nodeID);
	ImGui::DockBuilderFinish(nodeID);
	for (int frame = 0; frame < 4; ++frame) {
		ImGui::NewFrame();
		PrepareDockNodes();
		Begin(renamed.c_str(), &open);
		ImGui::TextUnformatted("固定Inspector");
		ImGui::End();
		Begin(duplicate.c_str(), &open);
		ImGui::TextUnformatted("複製Inspector");
		ImGui::End();
		ImGui::Render();
	}
	const auto* docked = ImGui::FindWindowByName(renamed.c_str());
	const auto* copied = ImGui::FindWindowByName(duplicate.c_str());
	if (!docked || !copied || docked->DockId != nodeID || copied->DockId != nodeID ||
		!node->TabBar || node->TabBar->Tabs.Size != 2) return false;
	const std::string saved = ImGui::SaveIniSettingsToMemory();
	if (saved.find("Inspector:inspector.primary") == std::string::npos ||
		saved.find("Inspector:duplicate") == std::string::npos) return false;
	std::cout << "Editor windows: icons, alignment, IDs, body style, DPI sizes, title drag, docking and layout passed\n";
	return true;
}
