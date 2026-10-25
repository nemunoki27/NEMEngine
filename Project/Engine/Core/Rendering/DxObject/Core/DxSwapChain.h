#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Core/DisplayOutputState.h>

// c++
#include <array>

// directX
#include <d3d12.h>
#include <dxgi1_6.h>

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
		void Create(WinApp* winApp, ID3D12Device* device,
			IDXGIFactory7* factory, ID3D12CommandQueue* queue,
			RTVDescriptor* rtvDescriptor, uint32_t width, uint32_t height,
			DXGI_FORMAT format, const Color4& clearColor,
			const DisplayOutputSettings& displayOutput);
		// バックバッファを指定サイズへ再作成する
		bool Resize(uint32_t width, uint32_t height);
		// 次のフレームで使用する表示出力を解決する
		bool RefreshDisplayOutput(const DisplayOutputSettings& requested, bool force = false);
		// バックバッファの形式変更が必要か
		bool NeedsOutputResize() const;
		// GPU完了後に表示出力を切り替える
		bool ApplyPendingDisplayOutput();

		//--------- accessor -----------------------------------------------------

		// スワップチェーンを取得する
		IDXGISwapChain4* Get() const { return swapChain_.Get(); }
		// 現在のバックバッファリソースを取得する
		ID3D12Resource* GetCurrentResource() const;
		// 現在のバックバッファインデックスを取得する
		uint32_t GetCurrentBackBufferIndex() const { return swapChain_->GetCurrentBackBufferIndex(); }
		// 現在のレンダーターゲット情報を取得する
		const RenderTarget& GetRenderTarget();
		const DXGI_SWAP_CHAIN_DESC1& GetDesc() const { return desc_; }
		const DisplayOutputSettings& GetDisplayOutputSettings() const { return outputState_.GetSettings(); }
		const DisplayOutputState& GetDisplayOutputState() const { return outputState_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		RenderTarget renderTarget_;
		ID3D12Device* device_ = nullptr;
		RTVDescriptor* rtvDescriptor_ = nullptr;

		// Flip Modelは最低2枚必要なため1FrameContextでも2枚確保する
		uint32_t bufferCount_ = 2;

		ComPtr<IDXGISwapChain4> swapChain_;
		DXGI_SWAP_CHAIN_DESC1 desc_{};
		DisplayOutputState outputState_{};
		DisplayOutputState pendingOutput_{};
		HWND hwnd_ = nullptr;
		IDXGIFactory* factory_ = nullptr;
		DXGI_FORMAT sdrFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;

		std::array<ComPtr<ID3D12Resource>,
			kGraphicsFrameContextCount> resources_;
		std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
			kGraphicsFrameContextCount> rtvHandles_;
		std::array<uint32_t,
			kGraphicsFrameContextCount> rtvIndices_ = {
			UINT32_MAX, UINT32_MAX
		};

		// バックバッファとRTVを取得して保持する
		bool CreateBackBufferResources(bool allocateDescriptors);
	};
}; // Engine
