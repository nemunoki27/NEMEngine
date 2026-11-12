#include "EditorWindowTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorWindowPresentation.h>
#include <Engine/Editor/UI/ImGui/ImGuiAppearance.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpersInternal.h>
#include <Engine/Editor/UI/Panels/Builtin/ViewportOrientationGizmo.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Renderer/Views/CameraViewProjection.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Editor/Core/Layout/EditorLayoutSerialization.h>

// c++
#include <cmath>
#include <iostream>
#include <string_view>
// imgui
#include <imgui_internal.h>

namespace {

	// 画像へ重ねた操作領域がカーソルと内容サイズを変えないことを確認する
	bool CheckViewportOverlay() {

		using namespace Engine;
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* gui = ImGui::CreateContext();
		const ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(gui); ImGui::SetCurrentContext(previous); });
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1200, 900);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		ImGui::GetStyle().ItemSpacing = ImVec2(4, 8);
		EditorState state;
		EditorPanelContext context;
		context.editorState = &state;
		ResolvedRenderView view;
		view.valid = true;
		view.perspective = CameraViewProjection::BuildManualPerspective(ManualRenderCameraState{}, 800, 450);
		context.sceneRenderView = &view;
		ViewportOrientationGizmo gizmo;
		ImVec2 imagePosition;
		ViewportOrientationLayout inputLayout;
		int pressed = 0, activated = 0;
		int orbits = 0, alignments = 0;
		Vector2 dragDelta{};
		bool valid = true;
		const ImVec2 sizes[]{ImVec2(800, 450), ImVec2(320, 180), ImVec2(144, 81)};
		for (int frame = 0; frame < 24; ++frame) {
			if (frame > 0 && frame < 12) io.AddMousePosEvent(imagePosition.x + 24, imagePosition.y + 24);
			if (frame == 2) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			if (frame == 3) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			if (frame == 13) {
				io.AddMousePosEvent(inputLayout.position.x + 5, inputLayout.position.y + 5);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			}
			if (frame == 14) io.AddMousePosEvent(inputLayout.position.x + 20, inputLayout.position.y + 20);
			if (frame == 15) {
				io.AddMousePosEvent(inputLayout.position.x + 35, inputLayout.position.y + 40);
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			}
			if (frame == 17) io.AddMousePosEvent(inputLayout.position.x + inputLayout.size * 0.5f,
				inputLayout.position.y + inputLayout.size * 0.5f);
			if (frame == 18) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			if (frame == 19) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(20, 20));
			ImGui::SetNextWindowSize(ImVec2(1000, 700));
			ImGui::Begin("ViewportOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
			const ImVec2 size = sizes[frame / 4 % 3];
			ImGui::BeginChild("ViewImage", ImVec2(size.x + 16, size.y + 16), ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			imagePosition = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(1), size);
			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			const ImVec2 content = ImGui::GetCurrentWindow()->DC.CursorMaxPos;
			pressed += DrawOverlayButton("OverlayProbe", ImVec2(imagePosition.x + 12, imagePosition.y + 12), ImVec2(24, 24)) ? 1 : 0;
			activated += ImGui::IsItemActivated() ? 1 : 0;
			state.sceneViewCamera.mode = frame >= 4 && frame < 12 ?
				SceneViewCameraMode::SelectedEntityCamera : SceneViewCameraMode::DebugManual;
			state.sceneViewPickDimension = frame >= 8 && frame < 12 ? SceneViewPickDimension::Type2D : SceneViewPickDimension::Type3D;
			state.drawSceneViewOrientation = frame < 20;
			const auto layout = ViewportOrientationGizmo::GetLayout(context, imagePosition, size);
			const bool visible = frame < 8 || (frame >= 12 && frame < 20);
			valid &= visible ? layout.size > 0.0f && layout.size <= ImGui::GetFontSize() * 3.25f : layout.size == 0.0f;
			state.drawSceneViewOrientation = false;
			valid &= ViewportOrientationGizmo::GetLayout(context, imagePosition, size).size == 0.0f;
			state.drawSceneViewOrientation = frame < 20;
			if (const auto command = gizmo.Draw(context, layout)) {
				if (command->action == SceneViewCameraGizmoAction::Orbit) { ++orbits; dragDelta += command->delta; }
				else { ++alignments; }
			}
			inputLayout = layout;
			const auto& dc = ImGui::GetCurrentWindow()->DC;
			valid &= cursor.x == dc.CursorPos.x && cursor.y == dc.CursorPos.y &&
				content.x == dc.CursorMaxPos.x && content.y == dc.CursorMaxPos.y && !dc.IsSetPos &&
				gizmo.IsActive() == (frame == 13 || frame == 14 || frame == 18);
			ImGui::EndChild();
			ImGui::End();
			ImGui::Render();
		}
		if (!valid || pressed != 1 || activated != 1 || orbits != 2 || alignments != 1 || dragDelta != Vector2(30, 35)) {
			std::cerr << "Viewport overlay changed layout or input: " << valid << " / " << pressed << " / " << activated <<
				" / " << orbits << " / " << alignments << " / " << dragDelta.x << ", " << dragDelta.y << '\n';
			return false;
		}
		return true;
	}

	// ツールの開閉状態と配置を同じレイアウトへ保存する
	bool CheckToolLayoutRoundTrip() {

		using namespace Engine;
		EditorLayoutSnapshot layout;
		layout.layoutID = "tool.layout";
		layout.displayName = "ツール編集用";
		layout.imguiIniData = "[Window][Tool]\nPos=2100,100\nSize=600,450\nCollapsed=0\nDockId=0x1111,0\n";
		layout.tools = { {"engine.animation_clip", true}, {"engine.timeline", true},
			{"engine.sceneViewCamera", false}, {"extension.tool", false} };
		const auto saved = EditorLayoutSerialization::MakeLayoutJson(layout, false);
		EditorLayoutSnapshot restored;
		bool imported = false;
		if (!EditorLayoutSerialization::ReadLayout(saved, restored, imported) ||
			saved != EditorLayoutSerialization::MakeLayoutJson(restored, imported)) return false;

		// 不正な項目を読み飛ばして有効なツールと配置を維持する
		auto invalid = saved;
		for (const auto& entry : nlohmann::json::array({nullptr, 10, "tool", nlohmann::json::object(),
			{{"toolID", 10}, {"open", true}}, {{"toolID", ""}, {"open", true}},
			{{"toolID", "missing.open"}}, {{"toolID", "invalid.open"}, {"open", "true"}}})) {
			invalid["tools"].push_back(entry);
		}
		if (!EditorLayoutSerialization::ReadLayout(invalid, restored, imported) ||
			saved != EditorLayoutSerialization::MakeLayoutJson(restored, imported)) return false;

		// 旧形式を読む際は直前に読み込んだツールの状態を持ち越さない
		auto legacy = saved;
		legacy.erase("tools");
		if (!EditorLayoutSerialization::ReadLayout(legacy, restored, imported) || !restored.tools.empty() ||
			restored.imguiIniData != layout.imguiIniData) return false;
		for (const auto& value : {nlohmann::json(nullptr), nlohmann::json::object(), nlohmann::json("invalid")}) {
			invalid = saved;
			invalid["tools"] = value;
			if (!EditorLayoutSerialization::ReadLayout(invalid, restored, imported) || !restored.tools.empty()) return false;
		}
		return true;
	}

	// 外部Windowの生成とタブの分離をImGuiの通常の更新経路で確認する
	bool CheckExternalDocking(Engine::EditorWindowPresentation::FrameStyle& frameStyle,
		ImGuiID nodeID, const char* first, const char* second) {

		using namespace Engine::EditorWindowPresentation;
		auto& io = ImGui::GetIO();
		auto& platform = ImGui::GetPlatformIO();
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
		io.ConfigDpiScaleFonts = true;
		platform.Platform_CreateWindow = [](ImGuiViewport* viewport) { viewport->PlatformHandle = viewport; };
		platform.Platform_DestroyWindow = [](ImGuiViewport* viewport) { viewport->PlatformHandle = nullptr; };
		platform.Platform_ShowWindow = [](ImGuiViewport*) {};
		platform.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2) {};
		platform.Platform_GetWindowPos = [](ImGuiViewport* viewport) { return viewport->Pos; };
		platform.Platform_SetWindowSize = [](ImGuiViewport*, ImVec2) {};
		platform.Platform_GetWindowSize = [](ImGuiViewport* viewport) { return viewport->Size; };
		platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
		platform.Platform_GetWindowDpiScale = [](ImGuiViewport* viewport) { return viewport->Pos.x >= 1920 ? 1.5f : 1.0f; };
		const Engine::ScopedCleanup cleanup([]() noexcept { ImGui::DestroyPlatformWindows(); });
		for (int monitorIndex = 0; monitorIndex < 2; ++monitorIndex) {
			ImGuiPlatformMonitor monitor;
			monitor.MainPos = monitor.WorkPos = ImVec2(static_cast<float>(monitorIndex * 1920), 0);
			monitor.MainSize = monitor.WorkSize = ImVec2(1920, 1080);
			monitor.DpiScale = monitorIndex == 0 ? 1.0f : 1.5f;
			platform.Monitors.push_back(monitor);
		}
		ImGui::GetMainViewport()->PlatformHandle = ImGui::GetMainViewport();
		ImGui::UpdatePlatformWindows();
		ImGui::DockBuilderSetNodePos(nodeID, ImVec2(2100, 100));
		auto drawFrame = [&]() {
			frameStyle.NewFrame();
			PrepareDockNodes();
			for (const char* title : {first, second}) {
				Begin(title);
				ImGui::TextUnformatted("本文の表示");
				ImGui::End();
			}
			frameStyle.Render();
			ImGui::UpdatePlatformWindows();
		};
		for (int frame = 0; frame < 5; ++frame) drawFrame();
		const auto* node = ImGui::DockBuilderGetNode(nodeID);
		if (!node || !node->HostWindow || !node->HostWindow->ViewportOwned || !node->TabBar ||
			std::abs(node->HostWindow->FontRefSize - 21.0f) > 0.01f ||
			std::abs(node->TabBar->BarRect.GetHeight() - 29.0f) > 0.01f) {
			std::cerr << "External dock header DPI style changed\n";
			return false;
		}
		const auto* viewport = node->HostWindow->Viewport;
		const ImGuiID previousID = viewport->ID;
		void* handle = viewport->PlatformHandle;
		ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), ImGui::FindWindowByName(second));
		for (int frame = 0; frame < 3; ++frame) drawFrame();
		const auto* remaining = ImGui::FindWindowByName(first);
		const auto* detached = ImGui::FindWindowByName(second);
		if (!handle || remaining->Viewport->PlatformHandle != handle || remaining->Viewport->ID == previousID ||
			remaining->Viewport->ID != remaining->ID || detached->DockId != 0) {
			std::cerr << "External dock viewport transfer failed\n";
			return false;
		}
		return true;
	}
}

bool NEMTests::TestEditorWindows() {

	using namespace Engine;
	using namespace EditorWindowPresentation;
	if (!CheckToolLayoutRoundTrip() || !CheckViewportOverlay()) return false;
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
	FrameStyle frameStyle;
	frameStyle.NewFrame();
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
	frameStyle.Render();
	if (!restored || !open || initialSize.x != 600.0f || initialSize.y != 450.0f) return false;
	// タイトルをドラッグしても共通の外観が移動操作を妨げないことを確認する
	for (int frame = 0; frame < 4; ++frame) {
		io.AddMousePosEvent(frame < 2 ? 150.0f : 350.0f, frame < 2 ? 110.0f : 210.0f);
		if (frame == 1) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		if (frame == 3) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		frameStyle.NewFrame();
		Begin(first.c_str(), &open);
		ImGui::TextUnformatted("パネル内の表示");
		ImGui::End();
		frameStyle.Render();
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
		frameStyle.NewFrame();
		PrepareDockNodes();
		Begin(renamed.c_str(), &open);
		ImGui::TextUnformatted("固定Inspector");
		ImGui::End();
		Begin(duplicate.c_str(), &open);
		ImGui::TextUnformatted("複製Inspector");
		ImGui::End();
		frameStyle.Render();
	}
	const auto* docked = ImGui::FindWindowByName(renamed.c_str());
	const auto* copied = ImGui::FindWindowByName(duplicate.c_str());
	if (!docked || !copied || docked->DockId != nodeID || copied->DockId != nodeID ||
		!node->TabBar || node->TabBar->Tabs.Size != 2) return false;
	if (!node->HostWindow || node->HostWindow->FontRefSize != 14.0f ||
		std::abs(node->TabBar->BarRect.GetHeight() - 22.0f) > 0.01f ||
		ImGui::GetStyle().FontSizeBase != bodyFont) {
		std::cerr << "Floating dock header or body style changed\n";
		return false;
	}
	const std::string saved = ImGui::SaveIniSettingsToMemory();
	if (saved.find("Inspector:inspector.primary") == std::string::npos ||
		saved.find("Inspector:duplicate") == std::string::npos) return false;
	if (!CheckExternalDocking(frameStyle, nodeID, renamed.c_str(), duplicate.c_str())) return false;
	std::cout << "Editor windows: icons, alignment, IDs, body style, DPI sizes, title drag, docking and layout passed\n";
	return true;
}
