#include "ImGuiDisplayOutput.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>

// c++
#include <algorithm>
#include <stdexcept>

using namespace Engine;

//============================================================================
//	ImGuiDisplayOutput classMethods
//============================================================================
void ImGuiDisplayOutput::Init(GraphicsCore& graphicsCore, ImGui_ImplDX12_InitInfo& info) {

	graphicsCore_ = &graphicsCore;
	info.OutputUserData = this;
	info.WindowOutputFn = ConfigureWindow;
	info.RenderOutputFn = RenderWindow;
	info.DestroyOutputFn = DestroyWindow;
	Begin();
}

void ImGuiDisplayOutput::Begin() {

	// UI操作中の変更は次のフレームでまとめて使用する
	requested_ = graphicsCore_->GetDXObject().GetFeatureController().GetPreferences().displayOutput;
	ImGuiViewport* main = ImGui::GetMainViewport();
	auto& window = windows_[main->ID];
	window.hwnd = WinApp::GetHwnd();
	window.state = graphicsCore_->GetDisplayOutputState();
	const auto& desc = graphicsCore_->GetSwapChainDesc();
	SyncComposition(window, desc.Width, desc.Height);
}

ImGui_ImplDX12_OutputConfig ImGuiDisplayOutput::BuildConfig(const WindowOutput& window) {

	const auto mode = window.state.GetSettings().mode;
	const auto format = DisplayOutputState::ResolveFormat(mode);
	ImGui_ImplDX12_OutputConfig config{};
	config.SwapChainFormat = format.buffer;
	config.RTVFormat = format.target;
	config.UIFormat = mode == DisplayOutputMode::SDR ? format.target : DXGI_FORMAT_R16G16B16A16_FLOAT;
	config.UIWhiteScale = mode == DisplayOutputMode::SDR ? 1.0f : window.state.GetUIWhiteNits() / 80.0f;
	config.Revision = window.state.GetRevision();
	return config;
}

void ImGuiDisplayOutput::SyncComposition(WindowOutput& window, uint32_t width, uint32_t height) {

	if (window.state.GetSettings().mode != DisplayOutputMode::HDR10) {
		window.composition.reset();
		return;
	}
	if (width == 0 || height == 0) return;
	if (window.composition && window.composition->GetRenderTarget().width == width &&
		window.composition->GetRenderTarget().height == height) return;

	// PQ変換前の合成だけ浮動小数点で保持する
	auto candidate = std::make_unique<RenderTexture2D>();
	RenderTextureCreateDesc desc{};
	desc.width = width;
	desc.height = height;
	desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.debugName = L"EditorHDRComposition";
	candidate->Create(graphicsCore_->GetDXObject().GetDevice(), &graphicsCore_->GetRTVDescriptor(),
		&graphicsCore_->GetSRVDescriptor(), desc);
	window.composition = std::move(candidate);
}

bool ImGuiDisplayOutput::ConfigureWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport,
	IDXGISwapChain3* swapChain, ImGui_ImplDX12_OutputConfig* config, int operation) {

	auto& owner = *static_cast<ImGuiDisplayOutput*>(info->OutputUserData);
	auto& window = owner.windows_[viewport->ID];
	window.hwnd = static_cast<HWND>(viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle);
	bool result = true;
	if (operation == 0) {

		// 記録を開始する前に表示先を確定する
		window.previous = window.state;
		window.state.Refresh(window.hwnd, owner.graphicsCore_->GetDXObject().GetDxgiFactory(), owner.requested_);
	} else if (operation == 2) {
		window.state.RestoreOutput(window.previous);
	} else if (!window.state.Apply(swapChain)) {

		// 希望設定を維持してSDRの再構成を要求する
		window.state.UseSDR(DisplayOutputFallback::ColorSpaceUnsupported);
		result = false;
	}
	*config = BuildConfig(window);
	owner.SyncComposition(window, static_cast<uint32_t>((std::max)(viewport->Size.x, 0.0f)),
		static_cast<uint32_t>((std::max)(viewport->Size.y, 0.0f)));
	return result;
}

void ImGuiDisplayOutput::Draw(ID3D12GraphicsCommandList* commandList) {

	DrawWindow(ImGui::GetMainViewport(), commandList, graphicsCore_->GetBackBufferRenderTarget());
}

void ImGuiDisplayOutput::DrawWindow(ImGuiViewport* viewport, ID3D12GraphicsCommandList* commandList,
	const RenderTarget& target) {

	auto& window = windows_.at(viewport->ID);
	ImGui_ImplDX12_OutputConfig config = BuildConfig(window);
	config.RTVFormat = target.format;
	if (window.state.GetSettings().mode == DisplayOutputMode::SDR) config.UIFormat = target.format;
	if (!window.composition) {

		// SDRとscRGBは表示先へ直接合成する
		commandList->OMSetRenderTargets(1, &target.rtvHandle, FALSE, nullptr);
		ImGui_ImplDX12_RenderDrawData(viewport->DrawData, commandList, &config);
		return;
	}

	// 映像とUIを線形scRGBで合成する
	auto& composition = *window.composition;
	composition.Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const auto handle = composition.GetRenderTarget().rtvHandle;
	const float clear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(handle, clear, 0, nullptr);
	commandList->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	ImGui_ImplDX12_RenderDrawData(viewport->DrawData, commandList, &config);
	composition.Transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (!renderer_ || !renderer_->PresentTexture(*graphicsCore_, commandList, composition.GetSRVGPUHandle(),
		target, window.state.GetSettings(), 80.0f)) {
		throw std::runtime_error("EditorのHDR出力変換に失敗しました");
	}
}

void ImGuiDisplayOutput::RenderWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport,
	ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE target) {

	auto& owner = *static_cast<ImGuiDisplayOutput*>(info->OutputUserData);
	const auto& window = owner.windows_.at(viewport->ID);
	const auto format = DisplayOutputState::ResolveFormat(window.state.GetSettings().mode);
	RenderTarget renderTarget{};
	renderTarget.width = static_cast<uint32_t>(viewport->Size.x);
	renderTarget.height = static_cast<uint32_t>(viewport->Size.y);
	renderTarget.format = format.target;
	renderTarget.rtvHandle = target;
	owner.DrawWindow(viewport, commandList, renderTarget);
}

void ImGuiDisplayOutput::DestroyWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport) {

	// 使用中の合成画像は既存の回収窓口へ渡す
	auto& owner = *static_cast<ImGuiDisplayOutput*>(info->OutputUserData);
	owner.windows_.erase(viewport->ID);
}

DisplayOutputSettings ImGuiDisplayOutput::GetSettings(HWND hwnd) const {

	for (const auto& [id, window] : windows_) {
		if (window.hwnd == hwnd) return window.state.GetSettings();
	}
	return graphicsCore_->GetDisplayOutputSettings();
}

void ImGuiDisplayOutput::SetImageWhiteLevel([[maybe_unused]] const ImDrawList* drawList, const ImDrawCmd* command) {

	ImGui_ImplDX12_SetImageWhiteLevel(*static_cast<const float*>(command->UserCallbackData));
}

void ImGuiDisplayOutput::DrawViewImage(ImTextureID texture, const ImVec2& size, float paperWhiteNits) {

	// 描画が遅れても参照が残るよう定数をDrawListへコピーする
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCallback(SetImageWhiteLevel, &paperWhiteNits, sizeof(paperWhiteNits));
	ImGui::Image(texture, size);
	drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
}

void ImGuiDisplayOutput::Finalize() {

	windows_.clear();
	renderer_ = nullptr;
	graphicsCore_ = nullptr;
}
