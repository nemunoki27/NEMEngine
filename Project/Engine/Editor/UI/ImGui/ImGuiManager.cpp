#include "ImGuiManager.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>

// c++
#include <filesystem>

// imgui
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

	LRESULT ForwardImGuiMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {

		return ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
	}
}

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
	WinApp::SetMessageHandler(ForwardImGuiMessage);

	// DX12初期化
	ImGui_ImplDX12_InitInfo dxInitInfo = {};
	dxInitInfo.Device = device;
	dxInitInfo.CommandQueue = commandQueue;
	dxInitInfo.NumFramesInFlight = bufferCount;
	dxInitInfo.RTVFormat = rtvFormat;
	dxInitInfo.DSVFormat = dsvFormat;
	dxInitInfo.SrvDescriptorHeap = srvDescriptor->GetDescriptorHeap();
	dxInitInfo.UserData = this;
	dxInitInfo.SrvDescriptorAllocFn = &ImGuiManager::AllocateSRVDescriptor;
	dxInitInfo.SrvDescriptorFreeFn = &ImGuiManager::FreeSRVDescriptor;
	ImGui_ImplDX12_Init(&dxInitInfo);

	//============================================================================
	//	imguiConfig
	//============================================================================
	// ImGuiのフォント設定
	ImFontConfig cfg{};
	cfg.FontNo = 0;
	
	const char* fontPath = "C:\\Windows\\Fonts\\meiryob.ttc";
	if (std::filesystem::exists(fontPath)) {
		io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());
	}
	else {
		// フォントがない場合のデフォルトフォントへのフォールバック
		io.Fonts->AddFontDefault();
	}

	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	auto C = [](int r, int g, int b, int a = 255) -> ImVec4 {
		return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
		};

	//============================================================================
	// Almost Pure Black Theme + Deep Orange Accent
	//============================================================================
	// ---- Base ----
	// ほぼ黒で紺っぽさを完全に消す
	const ImVec4 bg0 = C(0, 0, 0);          // WindowBg
	const ImVec4 bg1 = C(2, 2, 2);          // Child/Popup
	const ImVec4 topbar = C(3, 3, 3);          // Title/Menu
	const ImVec4 panel = C(6, 6, 6);          // Frame/Button/Header
	const ImVec4 panelH = C(12, 12, 12);       // Hover
	const ImVec4 panelA = C(18, 18, 18);       // Active
	const ImVec4 border = C(28, 28, 28, 130);  // Border

	// ---- Accent ----
	// 黒背景向けの暗め・濃いブルー
	const ImVec4 accent = C(12, 45, 115, 255);
	const ImVec4 accentH = C(18, 62, 150, 230);
	const ImVec4 accentA = C(26, 82, 190, 255);
	const ImVec4 accentLo = C(12, 45, 115, 70);

	// Text
	colors[ImGuiCol_Text] = C(125, 125, 125);
	colors[ImGuiCol_TextDisabled] = C(78, 78, 78, 153);

	// Window
	colors[ImGuiCol_WindowBg] = bg0;
	colors[ImGuiCol_ChildBg] = bg0;
	colors[ImGuiCol_PopupBg] = C(2, 2, 2, 245);

	// Borders
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_BorderShadow] = C(0, 0, 0, 0);

	// Frame
	colors[ImGuiCol_FrameBg] = C(5, 5, 5);
	colors[ImGuiCol_FrameBgHovered] = C(10, 10, 10);
	colors[ImGuiCol_FrameBgActive] = C(0, 13, 85, 255);

	// Titlebar
	colors[ImGuiCol_TitleBg] = topbar;
	colors[ImGuiCol_TitleBgActive] = C(5, 5, 5);
	colors[ImGuiCol_TitleBgCollapsed] = C(0, 0, 0);
	colors[ImGuiCol_MenuBarBg] = C(4, 4, 4);

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg] = bg0;
	colors[ImGuiCol_ScrollbarGrab] = C(12, 12, 12);
	colors[ImGuiCol_ScrollbarGrabHovered] = C(20, 20, 20);
	colors[ImGuiCol_ScrollbarGrabActive] = C(32, 32, 32);

	// Checkmark / Slider
	colors[ImGuiCol_CheckMark] = C(0, 40, 255, 255);
	colors[ImGuiCol_CheckboxSelectedBg] = C(7, 7, 7, 255);
	colors[ImGuiCol_SliderGrab] = C(120, 120, 120, 150);
	colors[ImGuiCol_SliderGrabActive] = accentA;

	// Buttons
	colors[ImGuiCol_Button] = panel;
	colors[ImGuiCol_ButtonHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_ButtonActive] = C(0, 7, 45, 255);

	// Header
	colors[ImGuiCol_Header] = C(13, 13, 13, 255);
	colors[ImGuiCol_HeaderHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_HeaderActive] = C(0, 7, 45, 255);

	// Separator / ResizeGrip
	colors[ImGuiCol_Separator] = border;
	colors[ImGuiCol_SeparatorHovered] = accentH;
	colors[ImGuiCol_SeparatorActive] = accentA;

	colors[ImGuiCol_ResizeGrip] = accentLo;
	colors[ImGuiCol_ResizeGripHovered] = C(18, 62, 150, 145);
	colors[ImGuiCol_ResizeGripActive] = C(0, 7, 45, 255);

	// Tabs
	colors[ImGuiCol_Tab] = topbar;
	colors[ImGuiCol_TabHovered] = C(0, 13, 85, 255);
	colors[ImGuiCol_TabSelected] = C(8, 8, 8);
	colors[ImGuiCol_TabSelectedOverline] = accentA;
	colors[ImGuiCol_TabDimmed] = C(1, 1, 1);
	colors[ImGuiCol_TabDimmedSelected] = C(5, 5, 5);
	colors[ImGuiCol_TabDimmedSelectedOverline] = C(18, 62, 150, 145);

	// Docking
	colors[ImGuiCol_DockingPreview] = C(12, 45, 115, 65);
	colors[ImGuiCol_DockingEmptyBg] = bg0;

	// Plots
	colors[ImGuiCol_PlotLines] = C(130, 130, 130);
	colors[ImGuiCol_PlotLinesHovered] = accentA;
	colors[ImGuiCol_PlotHistogram] = C(130, 130, 130);
	colors[ImGuiCol_PlotHistogramHovered] = accentA;

	// Selection / DragDrop
	colors[ImGuiCol_TextSelectedBg] = accentLo;
	colors[ImGuiCol_DragDropTarget] = C(0, 15, 98);

	// Nav highlight
	colors[ImGuiCol_NavHighlight] = accentA;
	colors[ImGuiCol_NavWindowingHighlight] = C(18, 62, 150, 170);
	colors[ImGuiCol_NavWindowingDimBg] = C(0, 0, 0, 180);
	colors[ImGuiCol_ModalWindowDimBg] = C(0, 0, 0, 205);

	colors[ImGuiCol_TableBorderStrong] = C(31, 31, 31);
	colors[ImGuiCol_TableBorderLight] = C(31, 31, 31);
	colors[ImGuiCol_TableRowBgAlt] = C(4, 4, 4);
	colors[ImGuiCol_TableHeaderBg] = C(2, 2, 2);

	// Shape / Layout
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

	WinApp::SetMessageHandler(nullptr);
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	imguiSRVIndices_.clear();
	srvDescriptor_ = nullptr;
	initialized_ = false;
}

void ImGuiManager::AllocateSRVDescriptor(ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle) {

	auto* manager = static_cast<ImGuiManager*>(info ? info->UserData : nullptr);
	if (!manager) {
		*outCPUHandle = {};
		*outGPUHandle = {};
		return;
	}
	manager->AllocateImGuiSRV(outCPUHandle, outGPUHandle);
}

void ImGuiManager::FreeSRVDescriptor(ImGui_ImplDX12_InitInfo* info,
	D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {

	auto* manager = static_cast<ImGuiManager*>(info ? info->UserData : nullptr);
	if (!manager) {
		return;
	}
	manager->FreeImGuiSRV(gpuHandle);
}

void ImGuiManager::AllocateImGuiSRV(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle) {

	if (!srvDescriptor_) {
		*outCPUHandle = {};
		*outGPUHandle = {};
		return;
	}

	const uint32_t index = srvDescriptor_->Allocate();
	*outCPUHandle = srvDescriptor_->GetCPUHandle(index);
	*outGPUHandle = srvDescriptor_->GetGPUHandle(index);
	imguiSRVIndices_[static_cast<uint64_t>(outGPUHandle->ptr)] = index;
}

void ImGuiManager::FreeImGuiSRV(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {

	const auto it = imguiSRVIndices_.find(static_cast<uint64_t>(gpuHandle.ptr));
	if (it == imguiSRVIndices_.end()) {
		return;
	}

	if (srvDescriptor_) {
		srvDescriptor_->Free(it->second);
	}
	imguiSRVIndices_.erase(it);
}
