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

//============================================================================
//	DxSwapChain classMethods
//============================================================================
void DxSwapChain::Create(WinApp* winApp, ID3D12Device* device, IDXGIFactory7* factory, ID3D12CommandQueue* queue, RTVDescriptor* rtvDescriptor,
	uint32_t width, uint32_t height, DXGI_FORMAT format, const Color4& clearColor) {

	device_ = device;
	rtvDescriptor_ = rtvDescriptor;

	DXGI_FORMAT bufferFormat = format;
	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		bufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		break;
	case DXGI_FORMAT_R10G10B10A2_UNORM:
		colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		break;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		break;
	default:
		format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	}

	// レンダーターゲットの設定
	renderTarget_.width = width;
	renderTarget_.height = height;
	renderTarget_.format = format;
	renderTarget_.clearColor = clearColor;

	swapChain_ = nullptr;
	desc_ = {};
	desc_.Width = width;
	desc_.Height = height;
	desc_.Format = bufferFormat;
	desc_.SampleDesc.Count = 1;
	desc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc_.BufferCount = kBufferCount;
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
	assert(SUCCEEDED(hr));

	// HDR formatのときは対応していればcolor spaceを設定する
	if (colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {

		UINT colorSpaceSupport = 0;
		if (SUCCEEDED(swapChain_->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport)) &&
			(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
			swapChain_->SetColorSpace1(colorSpace);
		}
	}

	Assert::Call(CreateBackBufferResources(true), "SwapChain back buffer creation failed.");
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
		kBufferCount, width, height, desc_.Format, desc_.Flags);
	if (!DxDredDiagnostics::CheckHRESULT(device_, resizeResult, "DxSwapChain::Resize/ResizeBuffers")) {
		Assert::Call(false, "SwapChain ResizeBuffers failed.");
		return false;
	}

	desc_.Width = width;
	desc_.Height = height;
	renderTarget_.width = width;
	renderTarget_.height = height;

	const bool created = CreateBackBufferResources(false);
	Assert::Call(created, "SwapChain back buffer recreation failed.");
	return created;
}

bool DxSwapChain::CreateBackBufferResources(bool allocateDescriptors) {

	if (!swapChain_ || !rtvDescriptor_) {
		return false;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = renderTarget_.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	for (uint32_t index = 0; index < kBufferCount; ++index) {

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
