#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// c++
#include <string>
// directX
#include <d3d12.h>

namespace Engine {

	// front
	class DSVDescriptor;
	class SRVDescriptor;
	class DxCommand;

	//============================================================================
	//	DepthTexture2D structures
	//============================================================================

	// 描画深度テクスチャ2Dの生成に必要な情報
	struct DepthTextureCreateDesc {

		// テクスチャのサイズ
		uint32_t width = 1;
		uint32_t height = 1;

		// リソースフォーマット
		DXGI_FORMAT resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		DXGI_FORMAT srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		// デバッグ用の名前
		std::wstring debugName;
	};

	//============================================================================
	//	DepthTexture2D class
	//	描画深度テクスチャ2D
	//============================================================================
	class DepthTexture2D {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DepthTexture2D() = default;
		~DepthTexture2D() = default;

		// リソース作成
		void Create(DSVDescriptor* dsvDescriptor, SRVDescriptor* srvDescriptor, const DepthTextureCreateDesc& desc);
		// 破棄
		void Destroy();

		// リソースの状態を遷移
		void Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState);

		//--------- accessor -----------------------------------------------------

		// リソースが有効かどうか
		bool IsValid() const { return resource_ != nullptr; }

		// テクスチャのサイズの取得
		uint32_t GetWidth() const { return width_; }
		uint32_t GetHeight() const { return height_; }

		// リソースの取得
		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// ハンドルの取得
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSVCPUHandle() const { return dsvCPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUHandle() const { return srvGPUHandle_; }

		// フォーマットの取得
		DXGI_FORMAT GetResourceFormat() const { return resourceFormat_; }
		DXGI_FORMAT GetDSVFormat() const { return dsvFormat_; }
		DXGI_FORMAT GetSRVFormat() const { return srvFormat_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// デスクリプタヒープ
		DSVDescriptor* dsvDescriptor_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		// 深度リソース
		ComPtr<ID3D12Resource> resource_;

		// DSVハンドル
		D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_{};

		// サイズ
		uint32_t width_ = 0;
		uint32_t height_ = 0;

		// 現在のリソース状態
		D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;

		// リソースフォーマット
		DXGI_FORMAT resourceFormat_{};
		DXGI_FORMAT dsvFormat_{};
		DXGI_FORMAT srvFormat_{};

		// デスクリプタヒープのインデックス
		uint32_t dsvIndex_ = UINT32_MAX;
		uint32_t srvIndex_ = UINT32_MAX;
	};
} // Engine