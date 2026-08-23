#include "DxSwapChain.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxDevice.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

//============================================================================
//	DxSwapChain classMethods
//============================================================================
namespace {

	struct SwapChainOutputFormat {

		DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DXGI_COLOR_SPACE_TYPE colorSpace =
			DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	};

	SwapChainOutputFormat ResolveSDRFormat(DXGI_FORMAT format) {

		SwapChainOutputFormat result{};
		switch (format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			result.bufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
			result.rtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			break;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		default:
			break;
		}
		return result;
	}

	SwapChainOutputFormat ResolveOutputFormat(DXGI_FORMAT sdrFormat,
		Engine::DisplayOutputMode mode) {

		if (mode == Engine::DisplayOutputMode::HDR10) {
			return {
				DXGI_FORMAT_R10G10B10A2_UNORM,
				DXGI_FORMAT_R10G10B10A2_UNORM,
				DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
			};
		}
		if (mode == Engine::DisplayOutputMode::ScRGB) {
			return {
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
			};
		}
		return ResolveSDRFormat(sdrFormat);
	}

	const char* GetDisplayOutputName(Engine::DisplayOutputMode mode) {

		switch (mode) {
		case Engine::DisplayOutputMode::HDR10:
			return "HDR10";
		case Engine::DisplayOutputMode::ScRGB:
			return "scRGB";
		case Engine::DisplayOutputMode::SDR:
		default:
			return "SDR";
		}
	}
}

void DxSwapChain::Create(WinApp* winApp, ID3D12Device* device,
	IDXGIFactory7* factory, ID3D12CommandQueue* queue,
	RTVDescriptor* rtvDescriptor, uint32_t width, uint32_t height,
	DXGI_FORMAT format, const Color4& clearColor,
	const DisplayOutputSettings& displayOutput) {

	device_ = device;
	rtvDescriptor_ = rtvDescriptor;
	bufferCount_ = (std::max)(
		2u, GraphicsFrameState::GetActiveCount());
	displayOutput_ = displayOutput;
	displayOutput_.paperWhiteNits = std::clamp(
		displayOutput_.paperWhiteNits, 80.0f, 1000.0f);
	displayOutput_.maxLuminanceNits = std::clamp(
		displayOutput_.maxLuminanceNits,
		displayOutput_.paperWhiteNits, 10000.0f);
	SwapChainOutputFormat outputFormat = ResolveOutputFormat(
		format, displayOutput_.mode);
	colorSpace_ = outputFormat.colorSpace;

	// レンダーターゲットの設定
	renderTarget_.width = width;
	renderTarget_.height = height;
	renderTarget_.format = outputFormat.rtvFormat;
	renderTarget_.clearColor = clearColor;

	swapChain_ = nullptr;
	desc_ = {};
	desc_.Width = width;
	desc_.Height = height;
	desc_.Format = outputFormat.bufferFormat;
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

	HRESULT hr = factory->CreateSwapChainForHwnd(
		queue, winApp->GetHwnd(), &desc_, nullptr, nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	Assert::Call(SUCCEEDED(hr), "SwapChainの作成に失敗しました");

	if (displayOutput_.mode != DisplayOutputMode::SDR &&
		!SupportsDisplayOutput()) {

		Logger::Output(LogType::Engine,
			"表示出力{}を利用できないためSDRへ戻します",
			GetDisplayOutputName(displayOutput_.mode));
		displayOutput_.mode = DisplayOutputMode::SDR;
		outputFormat = ResolveOutputFormat(format, displayOutput_.mode);
		colorSpace_ = outputFormat.colorSpace;
		desc_.Format = outputFormat.bufferFormat;
		renderTarget_.format = outputFormat.rtvFormat;
		const HRESULT fallbackResult = swapChain_->ResizeBuffers(
			bufferCount_, width, height, desc_.Format, desc_.Flags);
		Assert::Call(DxDredDiagnostics::CheckHRESULT(device_, fallbackResult,
			"DxSwapChain::Create/SDRFallback"),
			"SwapChainのSDR切り替えに失敗しました");
	}
	Assert::Call(ApplyDisplayOutput(),
		"SwapChainの表示出力設定に失敗しました");

	Assert::Call(CreateBackBufferResources(true), "SwapChainのBackBuffer作成に失敗しました");
	Logger::Output(LogType::Engine, "表示出力: {} ({:.0f}/{:.0f} nits)",
		GetDisplayOutputName(displayOutput_.mode),
		displayOutput_.paperWhiteNits,
		displayOutput_.maxLuminanceNits);
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
	if (!DxDredDiagnostics::CheckHRESULT(device_, resizeResult, "DxSwapChain::Resize/ResizeBuffers")) {
		Assert::Call(false, "SwapChainのResizeBuffersに失敗しました");
		return false;
	}

	desc_.Width = width;
	desc_.Height = height;
	renderTarget_.width = width;
	renderTarget_.height = height;
	if (!ApplyDisplayOutput()) {
		return false;
	}

	const bool created = CreateBackBufferResources(false);
	Assert::Call(created, "SwapChainのBackBuffer再作成に失敗しました");
	return created;
}

bool DxSwapChain::SupportsDisplayOutput() const {

	UINT colorSpaceSupport = 0;
	if (FAILED(swapChain_->CheckColorSpaceSupport(
		colorSpace_, &colorSpaceSupport)) ||
		!(colorSpaceSupport &
			DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
		return false;
	}

	ComPtr<IDXGIOutput> output{};
	if (FAILED(swapChain_->GetContainingOutput(output.GetAddressOf()))) {
		return false;
	}
	ComPtr<IDXGIOutput6> output6{};
	if (FAILED(output.As(&output6))) {
		return false;
	}
	DXGI_OUTPUT_DESC1 outputDesc{};
	if (FAILED(output6->GetDesc1(&outputDesc))) {
		return false;
	}
	return outputDesc.ColorSpace ==
		DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

bool DxSwapChain::ApplyDisplayOutput() {

	UINT colorSpaceSupport = 0;
	if (FAILED(swapChain_->CheckColorSpaceSupport(
		colorSpace_, &colorSpaceSupport)) ||
		!(colorSpaceSupport &
			DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ||
		FAILED(swapChain_->SetColorSpace1(colorSpace_))) {
		return false;
	}

	if (displayOutput_.mode != DisplayOutputMode::HDR10) {
		return SUCCEEDED(swapChain_->SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr));
	}

	DXGI_HDR_METADATA_HDR10 metadata{};
	metadata.RedPrimary[0] = 35400;
	metadata.RedPrimary[1] = 14600;
	metadata.GreenPrimary[0] = 8500;
	metadata.GreenPrimary[1] = 39850;
	metadata.BluePrimary[0] = 6550;
	metadata.BluePrimary[1] = 2300;
	metadata.WhitePoint[0] = 15635;
	metadata.WhitePoint[1] = 16450;
	metadata.MaxMasteringLuminance = static_cast<uint32_t>(
		displayOutput_.maxLuminanceNits * 10000.0f);
	metadata.MinMasteringLuminance = 50;
	metadata.MaxContentLightLevel = static_cast<uint16_t>(
		displayOutput_.maxLuminanceNits);
	metadata.MaxFrameAverageLightLevel = static_cast<uint16_t>(
		displayOutput_.maxLuminanceNits * 0.5f);
	return SUCCEEDED(swapChain_->SetHDRMetaData(
		DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata));
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
		if (!DxDredDiagnostics::CheckHRESULT(device_, getBufferResult, "DxSwapChain::CreateBackBufferResources/GetBuffer")) {
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
