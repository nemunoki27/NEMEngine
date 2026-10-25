#include "DxSwapChain.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <stdexcept>

//============================================================================
//	DxSwapChain classMethods
//============================================================================
void DxSwapChain::Create(WinApp* winApp, ID3D12Device* device,
	IDXGIFactory7* factory, ID3D12CommandQueue* queue,
	RTVDescriptor* rtvDescriptor, uint32_t width, uint32_t height,
	DXGI_FORMAT format, const Color4& clearColor,
	const DisplayOutputSettings& displayOutput) {

	if (!winApp || !device || !factory || !queue || !rtvDescriptor || width == 0 || height == 0) {
		throw std::invalid_argument("SwapChainの作成引数が不正です");
	}
	if (swapChain_) throw std::logic_error("SwapChainは作成済みです");
	device_ = device;
	hwnd_ = winApp->GetHwnd();
	factory_ = factory;
	sdrFormat_ = format;
	rtvDescriptor_ = rtvDescriptor;
	// 表示用の2枚は実行フレーム数から独立させる
	bufferCount_ = 2;
	outputState_.Refresh(hwnd_, factory_, displayOutput, true);
	pendingOutput_ = outputState_;
	DisplayOutputFormat outputFormat = DisplayOutputState::ResolveFormat(outputState_.GetSettings().mode, sdrFormat_);

	// レンダーターゲットの設定
	renderTarget_.width = width;
	renderTarget_.height = height;
	renderTarget_.format = outputFormat.target;
	renderTarget_.clearColor = clearColor;

	swapChain_ = nullptr;
	desc_ = {};
	desc_.Width = width;
	desc_.Height = height;
	desc_.Format = outputFormat.buffer;
	desc_.SampleDesc.Count = 1;
	desc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc_.BufferCount = bufferCount_;
	desc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc_.Scaling = DXGI_SCALING_NONE;
	// FPS制限解除
	BOOL allowTearing = FALSE;
	if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing) {
		desc_.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	// 作成した基本Interfaceから必要なInterfaceを取得する
	ComPtr<IDXGISwapChain1> candidate;
	const HRESULT result = factory->CreateSwapChainForHwnd(queue, winApp->GetHwnd(), &desc_, nullptr, nullptr, &candidate);
	if (!DxDREDDiagnostics::CheckHRESULT(device_, result, "DxSwapChain::Create")) {
		throw std::runtime_error("SwapChainの作成に失敗しました");
	}
	if (!DxDREDDiagnostics::CheckHRESULT(device_, candidate.As(&swapChain_), "DxSwapChain::Create/Interface")) {
		throw std::runtime_error("SwapChainのInterfaceを取得できませんでした");
	}

	if (!outputState_.Apply(swapChain_.Get())) {

		// 表示先が受け付けない形式はSDRへ戻す
		outputState_.UseSDR(DisplayOutputFallback::ColorSpaceUnsupported);
		outputFormat = DisplayOutputState::ResolveFormat(DisplayOutputMode::SDR, sdrFormat_);
		desc_.Format = outputFormat.buffer;
		renderTarget_.format = outputFormat.target;
		if (FAILED(swapChain_->ResizeBuffers(bufferCount_, width, height, desc_.Format, desc_.Flags)) ||
			!outputState_.Apply(swapChain_.Get())) throw std::runtime_error("SwapChainのSDR切替に失敗しました");
	}
	pendingOutput_ = outputState_;

	if (!CreateBackBufferResources(true)) throw std::runtime_error("SwapChainのBackBuffer作成に失敗しました");
	Logger::Output(LogType::Engine, "表示出力: {} ({:.0f}/{:.0f} nits)",
		static_cast<uint32_t>(outputState_.GetSettings().mode),
		outputState_.GetSettings().paperWhiteNits, outputState_.GetSettings().maxLuminanceNits);
}

bool DxSwapChain::Resize(uint32_t width, uint32_t height) {

	if (!swapChain_ || width == 0 || height == 0) {
		return false;
	}
	if (desc_.Width == width && desc_.Height == height) {
		return true;
	}

	for (ComPtr<ID3D12Resource>& resource : resources_) {
		resource.Reset();
	}

	const HRESULT resizeResult = swapChain_->ResizeBuffers(
		bufferCount_, width, height, desc_.Format, desc_.Flags);
	if (!DxDREDDiagnostics::CheckHRESULT(device_, resizeResult, "DxSwapChain::Resize/ResizeBuffers")) {
		throw std::runtime_error("SwapChainのResizeBuffersに失敗しました");
	}

	desc_.Width = width;
	desc_.Height = height;
	renderTarget_.width = width;
	renderTarget_.height = height;
	if (!outputState_.Apply(swapChain_.Get())) throw std::runtime_error("SwapChainの表示出力設定に失敗しました");

	if (!CreateBackBufferResources(false)) throw std::runtime_error("SwapChainのBackBuffer再作成に失敗しました");
	return true;
}

bool DxSwapChain::RefreshDisplayOutput(const DisplayOutputSettings& requested, bool force) {

	return pendingOutput_.Refresh(hwnd_, factory_, requested, force);
}

bool DxSwapChain::NeedsOutputResize() const {

	return desc_.Format != DisplayOutputState::ResolveFormat(pendingOutput_.GetSettings().mode, sdrFormat_).buffer;
}

bool DxSwapChain::ApplyPendingDisplayOutput() {

	const DisplayOutputState previous = outputState_;
	if (!NeedsOutputResize()) {

		// 輝度だけの変更では描画資源に触れない
		if (!pendingOutput_.Apply(swapChain_.Get())) {
			if (!previous.Apply(swapChain_.Get())) throw std::runtime_error("SwapChainの表示設定を復元できませんでした");
			pendingOutput_.RestoreOutput(previous);
			outputState_ = pendingOutput_;
			return false;
		}
		outputState_ = pendingOutput_;
		return true;
	}
	auto apply = [&](DisplayOutputState& state) {
		const auto format = DisplayOutputState::ResolveFormat(state.GetSettings().mode, sdrFormat_);
		if (desc_.Format != format.buffer) {

			// GPU完了後にバックバッファ参照を解除する
			for (auto& resource : resources_) resource.Reset();
			if (FAILED(swapChain_->ResizeBuffers(bufferCount_, desc_.Width, desc_.Height, format.buffer, desc_.Flags))) return false;
			desc_.Format = format.buffer;
		}
		renderTarget_.format = format.target;
		return state.Apply(swapChain_.Get()) && (resources_[0] || CreateBackBufferResources(false));
	};
	if (apply(pendingOutput_)) {
		outputState_ = pendingOutput_;
		return true;
	}

	// HDR切替が失敗した場合は利用可能なSDR出力を試す
	if (pendingOutput_.GetSettings().mode != DisplayOutputMode::SDR) {
		pendingOutput_.UseSDR(DisplayOutputFallback::ColorSpaceUnsupported);
		if (apply(pendingOutput_)) {
			outputState_ = pendingOutput_;
			return true;
		}
	}
	DisplayOutputState recovery = previous;
	if (!apply(recovery)) throw std::runtime_error("SwapChainの表示出力を復元できませんでした");
	pendingOutput_.RestoreOutput(previous);
	outputState_ = pendingOutput_;
	Logger::Output(LogType::Engine, spdlog::level::err, "表示出力の変更に失敗したため旧出力を維持します");
	return false;
}

bool DxSwapChain::CreateBackBufferResources(bool allocateDescriptors) {

	if (!swapChain_ || !rtvDescriptor_) {
		return false;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = renderTarget_.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	for (uint32_t index = 0; index < bufferCount_; ++index) {

		const HRESULT getBufferResult = swapChain_->GetBuffer(index, IID_PPV_ARGS(&resources_[index]));
		if (!DxDREDDiagnostics::CheckHRESULT(device_, getBufferResult, "DxSwapChain::CreateBackBufferResources/GetBuffer")) {
			return false;
		}
		resources_[index]->SetName((L"backBufferResource" + std::to_wstring(index)).c_str());

		if (allocateDescriptors) {
			rtvDescriptor_->Create(rtvIndices_[index], rtvHandles_[index], resources_[index].Get(), rtvDesc);
		} else {
			rtvDescriptor_->Recreate(rtvIndices_[index], rtvHandles_[index], resources_[index].Get(), rtvDesc);
		}
	}
	return true;
}

ID3D12Resource* DxSwapChain::GetCurrentResource() const {

	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
	return resources_[backBufferIndex].Get();
}

const RenderTarget& DxSwapChain::GetRenderTarget() {

	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
	renderTarget_.rtvHandle.ptr = rtvHandles_[backBufferIndex].ptr;
	return renderTarget_;
}
