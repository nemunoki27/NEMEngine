#include "DxSwapChain.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxDevice.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>

//============================================================================
//	DxSwapChain classMethods
//============================================================================
void DxSwapChain::Create(WinApp* winApp, IDXGIFactory7* factory, ID3D12CommandQueue* queue, RTVDescriptor* rtvDescriptor,
	uint32_t width, uint32_t height, DXGI_FORMAT format, const Color4& clearColor) {

	// flip modelのswapchain bufferは_SRGB不可なので、RTVは_SRGBやHDRのままbufferはUNORM基底へ分離する
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
		// HDR10はST2084 PQのRec2020
		colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		break;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		// scRGB HDRはlinearのRec709
		colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		break;
	default:
		// flip model非対応formatは安全側のsRGBへ落とす
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
	// tearing対応GPUならALLOW_TEARINGを付けてFPS制限解除時にvsyncの上限を外せるようにする
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

	// バックバッファのリソースとRTVを作成
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	uint32_t unusedIndex = UINT32_MAX;
	for (uint32_t index = 0; index < kBufferCount; ++index) {

		hr = swapChain_->GetBuffer(index, IID_PPV_ARGS(&resources_[index]));
		assert(SUCCEEDED(hr));
		resources_[index]->SetName((L"backBufferResource" + std::to_wstring(index)).c_str());

		// RTV作成
		rtvDescriptor->Create(unusedIndex, rtvHandles_[index], resources_[index].Get(), rtvDesc);
	}
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
