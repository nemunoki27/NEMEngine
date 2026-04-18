#include "ImGuiManager.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>

// imgui
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

//============================================================================
//	ImGuiManager classMethods
//============================================================================

void ImGuiManager::Init(HWND hwnd, UINT bufferCount, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
	SRVDescriptor* srvDescriptor, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat) {

	if (initialized_) {
		return;
	}

	srvDescriptor_ = srvDescriptor;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// コンフィグ設定
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();
	//Win32初期化
	ImGui_ImplWin32_Init(hwnd);

	// DX12初期化 
	ImGui_ImplDX12_InitInfo dxInitInfo = {};
	dxInitInfo.Device = device;
	dxInitInfo.CommandQueue = commandQueue;
	dxInitInfo.NumFramesInFlight = bufferCount;
	dxInitInfo.RTVFormat = rtvFormat;
	dxInitInfo.DSVFormat = dsvFormat;
	dxInitInfo.SrvDescriptorHeap = srvDescriptor->GetDescriptorHeap();
	dxInitInfo.LegacySingleSrvCpuDescriptor = srvDescriptor->GetDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	dxInitInfo.LegacySingleSrvGpuDescriptor = srvDescriptor->GetDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplDX12_Init(&dxInitInfo);

	// SRVを進める
	srvDescriptor->Allocate();

	//========================================================================
	//	imguiConfig
	//========================================================================

	// ImGuiのフォント設定
	ImFontConfig cfg{};
	cfg.FontNo = 0;
	const char* fontPath = "C:\\Windows\\Fonts\\meiryob.ttc";
	io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 24.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());

	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	auto C = [](int r, int g, int b, int a = 255) -> ImVec4 {
		return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
		};

	// ---- Even darker base (前回よりさらに1段暗く) ----
	const ImVec4 bg0 = C(6, 7, 8);         // WindowBg
	const ImVec4 bg1 = C(8, 9, 11);        // Child/Popup
	const ImVec4 topbar = C(10, 11, 13);        // Title/Menu
	const ImVec4 panel = C(14, 16, 18);        // Frame/Button/Header
	const ImVec4 panelH = C(20, 22, 26);        // Hover
	const ImVec4 panelA = C(28, 31, 36);        // Active (非アクセント)
	const ImVec4 border = C(45, 48, 52, 110);   // Border（暗め＆控えめ）

	// ---- Accent (指定の赤系：色合い/濃さを統一) ----
	const ImVec4 accent = C(216, 31, 0, 255);
	const ImVec4 accentH = C(216, 31, 0, 210);
	const ImVec4 accentA = C(216, 31, 0, 255); // ←指定どおり
	const ImVec4 accentLo = C(216, 31, 0, 75);

	// Text（暗背景でも読める程度に）
	colors[ImGuiCol_Text] = C(200, 200, 200);
	colors[ImGuiCol_TextDisabled] = C(95, 95, 95);

	// Window
	colors[ImGuiCol_WindowBg] = bg0;
	colors[ImGuiCol_ChildBg] = bg0;
	colors[ImGuiCol_PopupBg] = C(8, 9, 11, 245);

	// Borders
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_BorderShadow] = C(0, 0, 0, 0);

	// Frame (Input/Slider/Combo)
	colors[ImGuiCol_FrameBg] = panel;
	colors[ImGuiCol_FrameBgHovered] = panelH;
	colors[ImGuiCol_FrameBgActive] = panelA;

	// Titlebar
	colors[ImGuiCol_TitleBg] = topbar;
	colors[ImGuiCol_TitleBgActive] = C(12, 13, 15);
	colors[ImGuiCol_TitleBgCollapsed] = C(8, 9, 11);

	// Menu bar
	colors[ImGuiCol_MenuBarBg] = topbar;

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg] = C(5, 6, 7);
	colors[ImGuiCol_ScrollbarGrab] = C(24, 26, 30);
	colors[ImGuiCol_ScrollbarGrabHovered] = C(34, 37, 42);
	colors[ImGuiCol_ScrollbarGrabActive] = C(44, 48, 54);

	// Checkmark / Slider grab
	colors[ImGuiCol_CheckMark] = accentA;
	colors[ImGuiCol_SliderGrab] = C(170, 170, 170, 165);
	colors[ImGuiCol_SliderGrabActive] = accentA;

	// Buttons
	colors[ImGuiCol_Button] = panel;
	colors[ImGuiCol_ButtonHovered] = panelH;
	colors[ImGuiCol_ButtonActive] = accentA;

	// Header (TreeNode / Selectable / CollapsingHeader)
	colors[ImGuiCol_Header] = panel;
	colors[ImGuiCol_HeaderHovered] = panelH;
	colors[ImGuiCol_HeaderActive] = accentH;

	// Separator / ResizeGrip
	colors[ImGuiCol_Separator] = border;
	colors[ImGuiCol_SeparatorHovered] = accentH;
	colors[ImGuiCol_SeparatorActive] = accentA;

	colors[ImGuiCol_ResizeGrip] = accentLo;
	colors[ImGuiCol_ResizeGripHovered] = C(216, 31, 0, 140);
	colors[ImGuiCol_ResizeGripActive] = accentA;

	// Tabs（Docking用）
	colors[ImGuiCol_Tab] = C(10, 11, 13);
	colors[ImGuiCol_TabHovered] = accentH;
	colors[ImGuiCol_TabSelected] = C(16, 18, 21);
	colors[ImGuiCol_TabSelectedOverline] = accentA;
	colors[ImGuiCol_TabDimmed] = C(8, 9, 11);
	colors[ImGuiCol_TabDimmedSelected] = C(12, 13, 15);
	colors[ImGuiCol_TabDimmedSelectedOverline] = C(216, 31, 0, 140);

	// Docking
	colors[ImGuiCol_DockingPreview] = C(216, 31, 0, 65);
	colors[ImGuiCol_DockingEmptyBg] = bg0;

	// Plots
	colors[ImGuiCol_PlotLines] = C(145, 145, 145);
	colors[ImGuiCol_PlotLinesHovered] = accentA;
	colors[ImGuiCol_PlotHistogram] = C(145, 145, 145);
	colors[ImGuiCol_PlotHistogramHovered] = accentA;

	// Selection / DragDrop
	colors[ImGuiCol_TextSelectedBg] = accentLo;
	colors[ImGuiCol_DragDropTarget] = accentA;

	// Nav highlight
	colors[ImGuiCol_NavHighlight] = accentA;
	colors[ImGuiCol_NavWindowingHighlight] = C(216, 31, 0, 170);
	colors[ImGuiCol_NavWindowingDimBg] = C(0, 0, 0, 170);
	colors[ImGuiCol_ModalWindowDimBg] = C(0, 0, 0, 190);

	// ---- Shape / Layout ----
	style.WindowRounding = 2.0f;
	style.ChildRounding = 2.0f;
	style.FrameRounding = 2.0f;
	style.ScrollbarRounding = 2.0f;
	style.GrabRounding = 2.0f;
	style.TabRounding = 2.0f;

	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;

	style.DockingSeparatorSize = 2.0f;

	initialized_ = true;
}

void ImGuiManager::Begin() {

	if (!initialized_) {
		return;
	}

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiManager::End() {

	if (!initialized_) {
		return;
	}

	ImGui::Render();
}

void ImGuiManager::Draw(ID3D12GraphicsCommandList* commandList) {

	if (!initialized_ || !srvDescriptor_) {
		return;
	}

	ID3D12DescriptorHeap* heaps[] = { srvDescriptor_->GetDescriptorHeap() };
	commandList->SetDescriptorHeaps(1, heaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiManager::Finalize() {

	if (!initialized_) {
		return;
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	srvDescriptor_ = nullptr;
	initialized_ = false;
}