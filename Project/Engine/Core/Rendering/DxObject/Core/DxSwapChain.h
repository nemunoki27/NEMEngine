#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
#include <dxgi1_6.h>
// c++
#include <array>

namespace Engine {

	// front
	class WinApp;
	class RTVDescriptor;
	class DxDevice;
	class DxCommand;

	//============================================================================
	//	DxSwapChain class
	// スワップチェーンとバックバッファのRTVを生成し、現在のターゲットを提供する
	//============================================================================
	class DxSwapChain {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxSwapChain() = default;
		~DxSwapChain() = default;

		// ウィンドウ/ファクトリ/キュー/RTVデスクリプタからスワップチェーンとRTVを作成する
		void Create(WinApp* winApp, ID3D12Device* device, IDXGIFactory7* factory, ID3D12CommandQueue* queue, RTVDescriptor* rtvDescriptor,
			uint32_t width, uint32_t height, DXGI_FORMAT format, const Color4& clearColor);
		// バックバッファを指定サイズへ再作成する
		bool Resize(uint32_t width, uint32_t height);

		//--------- accessor -----------------------------------------------------

		// スワップチェーンを取得する
		IDXGISwapChain4* Get() const { return swapChain_.Get(); }
		// 現在のバックバッファリソースを取得する
		ID3D12Resource* GetCurrentResource() const;
		// 現在のレンダーターゲット情報を取得する
		const RenderTarget& GetRenderTarget();
		const DXGI_SWAP_CHAIN_DESC1& GetDesc() const { return desc_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		RenderTarget renderTarget_;
		ID3D12Device* device_ = nullptr;
		RTVDescriptor* rtvDescriptor_ = nullptr;

		// フレームバッファ数
		static const constexpr uint32_t kBufferCount = 2;

		ComPtr<IDXGISwapChain4> swapChain_;
		DXGI_SWAP_CHAIN_DESC1 desc_{};

		std::array<ComPtr<ID3D12Resource>, kBufferCount> resources_;
		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kBufferCount> rtvHandles_;
		std::array<uint32_t, kBufferCount> rtvIndices_ = { UINT32_MAX, UINT32_MAX };

		// バックバッファとRTVを取得して保持する
		bool CreateBackBufferResources(bool allocateDescriptors);
	};
}; // Engine
