#include "ImGuiManager.h"
#include "ImGuiAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <stdexcept>

// windows
#include <shellapi.h>

// imgui
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

using namespace Engine;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

	void ApplyDarkWindowFrame(HWND hwnd) {

		// OSが提供するWindowの配色設定を取得する
		using DwmSetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

		HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
		if (!dwmapi) {
			return;
		}

		auto setWindowAttribute =
			reinterpret_cast<DwmSetWindowAttributeFunction>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
		if (setWindowAttribute) {

			const BOOL enabled = TRUE;
			constexpr DWORD kUseImmersiveDarkMode = 20;
			constexpr DWORD kUseImmersiveDarkModeLegacy = 19;
			if (FAILED(setWindowAttribute(hwnd, kUseImmersiveDarkMode, &enabled, sizeof(enabled)))) {
				setWindowAttribute(hwnd, kUseImmersiveDarkModeLegacy, &enabled, sizeof(enabled));
			}
		}
		FreeLibrary(dwmapi);
	}
}

ImGuiManager* ImGuiManager::instance_ = nullptr;

//============================================================================
//	ImGuiManager classMethods
//============================================================================
void ImGuiManager::Init(HWND hwnd, UINT bufferCount, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
	SRVDescriptor* srvDescriptor, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat) try {

	if (initialized_) {
		return;
	}

	srvDescriptor_ = srvDescriptor;
	instance_ = this;
	ApplyDarkWindowFrame(hwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	contextCreated_ = true;

	// コンフィグ設定
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigViewportsNoAutoMerge = false;
	io.ConfigViewportsNoTaskBarIcon = false;
	io.ConfigViewportsNoDecoration = false;
	io.ConfigDpiScaleFonts = true;
	io.ConfigDpiScaleViewports = true;

	ImGui::StyleColorsDark();
	// Win32の入力とWindow処理を接続する
	platformInitialized_ = ImGui_ImplWin32_Init(hwnd);
	if (!platformInitialized_) {
		throw std::runtime_error("ImGuiのWindow初期化に失敗しました");
	}
	WinApp::SetMessageHandler(ForwardWindowMessage);

	// DX12初期化
	ImGui_ImplDX12_InitInfo dxInitInfo = {};
	dxInitInfo.Device = device;
	dxInitInfo.CommandQueue = commandQueue;
	dxInitInfo.NumFramesInFlight = bufferCount;
	dxInitInfo.RTVFormat = rtvFormat;
	dxInitInfo.DSVFormat = dsvFormat;
	dxInitInfo.SrvDescriptorHeap = srvDescriptor->GetDescriptorHeap();
	dxInitInfo.UserData = this;
	dxInitInfo.WaitForGPUFn = &ImGuiManager::WaitForGPU;
	dxInitInfo.ResourceRetireFn = &ImGuiManager::RetireResource;
	dxInitInfo.SrvDescriptorAllocFn = &ImGuiManager::AllocateSRVDescriptor;
	dxInitInfo.SrvDescriptorFreeFn = &ImGuiManager::FreeSRVDescriptor;
	rendererInitialized_ = ImGui_ImplDX12_Init(&dxInitInfo);
	if (!rendererInitialized_) {
		throw std::runtime_error("ImGuiの描画初期化に失敗しました");
	}

	// フォントと外部Window用の外観を揃える
	ImGuiAppearance::ConfigureFont(io);
	ImGuiAppearance::ApplyTheme(ImGui::GetStyle(), (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0);

	initialized_ = true;
} catch (...) {

	// 初期化途中の接続と資源を戻す
	Finalize();
	throw;
}

void ImGuiManager::Begin() {

	if (!initialized_) {
		return;
	}

	ImGui_ImplDX12_NewFrame();
	CheckGPUFailure();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiManager::End() {

	if (!initialized_) {
		return;
	}

	ImGui::Render();
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		ImGui::UpdatePlatformWindows();
		CheckGPUFailure();
		RegisterPlatformWindows();
	}
}

void ImGuiManager::Draw(ID3D12GraphicsCommandList* commandList) {

	if (!initialized_ || !srvDescriptor_) {
		return;
	}

	ID3D12DescriptorHeap* heaps[] = {srvDescriptor_->GetDescriptorHeap()};
	commandList->SetDescriptorHeaps(1, heaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	CheckGPUFailure();
}

void ImGuiManager::DrawPlatformWindows() {

	if (!initialized_ || !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
		return;
	}
	ImGui::RenderPlatformWindowsDefault();
	CheckGPUFailure();
}

void ImGuiManager::Finalize() {

	if (contextCreated_) {

		// 外部Windowの接続を戻してBackendを終了
		RestorePlatformWindowProcedures();
		WinApp::SetMessageHandler(nullptr);
		if (rendererInitialized_) {
			ImGui_ImplDX12_Shutdown();
			rendererInitialized_ = false;
		}
		if (platformInitialized_) {
			ImGui_ImplWin32_Shutdown();
			platformInitialized_ = false;
		}
		ImGui::DestroyContext();
		contextCreated_ = false;
	}

	// 初期化前の失敗でも借用を解除
	imguiSRVIndices_.clear();
	platformWindowProcedures_.clear();
	srvDescriptor_ = nullptr;
	if (instance_ == this) {
		instance_ = nullptr;
	}
	initialized_ = false;
	gpuFailed_ = false;
}

bool ImGuiManager::WaitForGPU(ImGui_ImplDX12_InitInfo* info, ID3D12Fence* fence, UINT64 value, HANDLE event) {

	// Backendの待機失敗を次の描画処理へ伝える
	auto& manager = *static_cast<ImGuiManager*>(info->UserData);
	const bool completed = fence   ? DxDREDDiagnostics::WaitForFence(info->Device, fence, value, event, "ImGui::Fence")
						   : event ? DxDREDDiagnostics::WaitForEvent(info->Device, event, "ImGui::Present")
								   : DxDREDDiagnostics::CheckDeviceState(info->Device, "ImGui::Device");
	manager.gpuFailed_ |= !completed;
	return completed;
}

void ImGuiManager::CheckGPUFailure() const {

	if (gpuFailed_) {
		throw std::runtime_error("ImGuiのGPU処理を継続できません");
	}
}

void ImGuiManager::RetireResource(ImGui_ImplDX12_InitInfo* info, ID3D12Object* resource) {

	// Backendの解放後も提出済みdrawの参照を保持する
	auto& manager = *static_cast<ImGuiManager*>(info->UserData);
	manager.srvDescriptor_->GetRetirementQueue().Retire(ComPtr<ID3D12Object>(resource));
}

void ImGuiManager::AllocateSRVDescriptor(
	ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle) {

	// 初期化時に接続した所有元へDescriptor要求を渡す
	auto& manager = *static_cast<ImGuiManager*>(info->UserData);
	manager.AllocateImGuiSRV(outCPUHandle, outGPUHandle);
}

void ImGuiManager::FreeSRVDescriptor(
	ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {

	// Backend終了まで有効な所有元へ返却する
	auto& manager = *static_cast<ImGuiManager*>(info->UserData);
	manager.FreeImGuiSRV(gpuHandle);
}

void ImGuiManager::AllocateImGuiSRV(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle) {

	if (!srvDescriptor_) {
		*outCPUHandle = {};
		*outGPUHandle = {};
		return;
	}

	const uint32_t index = srvDescriptor_->Allocate();
	// 使用番号を記録してBackendへHandleを渡す
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
		// GPU完了後にDescriptorを再利用する
		srvDescriptor_->Retire(it->second, {});
	}
	imguiSRVIndices_.erase(it);
}

void ImGuiManager::RegisterPlatformWindows() {

	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
	for (int32_t index = 1; index < platformIO.Viewports.Size; ++index) {

		ImGuiViewport* viewport = platformIO.Viewports[index];
		HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
		if (!hwnd || platformWindowProcedures_.contains(hwnd)) {
			continue;
		}

		ApplyDarkWindowFrame(hwnd);
		DragAcceptFiles(hwnd, TRUE);
		WNDPROC previous = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ImGuiManager::PlatformWindowProc)));
		if (previous) {
			platformWindowProcedures_.emplace(hwnd, previous);
		}
	}
}

void ImGuiManager::RestorePlatformWindowProcedures() {

	for (const auto& [hwnd, procedure] : platformWindowProcedures_) {
		if (IsWindow(hwnd)) {
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure));
		}
	}
	platformWindowProcedures_.clear();
}

bool ImGuiManager::IsEditorWindow(HWND hwnd) const {

	return hwnd == WinApp::GetHwnd() || platformWindowProcedures_.contains(hwnd);
}

LRESULT ImGuiManager::PlatformWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {

	if (!instance_) {
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}

	auto found = instance_->platformWindowProcedures_.find(hwnd);
	if (found == instance_->platformWindowProcedures_.end()) {
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}
	WNDPROC previous = found->second;
	instance_->shortcutState_.ProcessMessage(message, wparam, lparam);

	switch (message) {
	case WM_SETFOCUS:
		if (Input* input = Input::TryGetInstance()) {
			input->SetWindowFocus(true);
		}
		break;
	case WM_KILLFOCUS:
		instance_->shortcutState_.Reset();
		if (Input* input = Input::TryGetInstance()) {
			input->SetWindowFocus(instance_->IsEditorWindow(reinterpret_cast<HWND>(wparam)));
		}
		break;
	case WM_DROPFILES:
		WinApp::HandleExternalFileDrop(hwnd, wparam);
		return 0;
	}

	const LRESULT result = CallWindowProcW(previous, hwnd, message, wparam, lparam);
	if (message == WM_NCDESTROY) {
		instance_->platformWindowProcedures_.erase(hwnd);
	}
	return result;
}

LRESULT Engine::ImGuiManager::ForwardWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {

	// ImGuiの消費状態に依存せず切替入力を保持
	instance_->shortcutState_.ProcessMessage(message, wparam, lparam);
	if (message == WM_KILLFOCUS || (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE)) {
		instance_->shortcutState_.Reset();
	}
	return ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
}

bool Engine::ImGuiManager::ConsumeHidePanelsShortcut() {

	return shortcutState_.Consume();
}
