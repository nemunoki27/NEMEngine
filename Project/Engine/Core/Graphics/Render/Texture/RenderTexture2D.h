#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxStructures.h>
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// c++
#include <string>

namespace Engine {

	// front
	class RTVDescriptor;
	class SRVDescriptor;
	class DxCommand;

	//============================================================================
	//	RenderTexture2D structures
	//============================================================================

	// 描画レンダーテクスチャ2Dの生成に必要な情報
	struct RenderTextureCreateDesc {

		// テクスチャのサイズ
		uint32_t width = 1;
		uint32_t height = 1;

		// RTVフォーマット
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Color4 clearColor = Color4::Black();

		// UAVを作成するかどうか
		bool createUAV = false;
		// デバッグ用の名前
		std::wstring debugName;
	};

	//============================================================================
	//	RenderTexture2D class
	//	描画色レンダーテクスチャ2D
	//============================================================================
	class RenderTexture2D {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderTexture2D() = default;
		~RenderTexture2D() = default;

		// リソース作成
		void Create(ID3D12Device* device, RTVDescriptor* rtvDescriptor,
			SRVDescriptor* srvDescriptor, const RenderTextureCreateDesc& desc);
		// 破棄
		void Destroy();

		// リソースの状態を遷移
		void Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState);

		//--------- accessor -----------------------------------------------------

		// RTVが有効かどうか
		bool IsValid() const { return resource_ != nullptr; }

		ID3D12Resource* GetResource() const { return resource_.Get(); }
		const RenderTarget& GetRenderTarget() const { return renderTarget_; }

		// GPUディスクリプタハンドルの取得
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUHandle() const { return srvGPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetUAVGPUHandle() const { return uavGPUHandle_; }

		// 現在のリソース状態の取得
		D3D12_RESOURCE_STATES GetCurrentState() const { return currentState_; }
		DXGI_FORMAT GetFormat() const { return format_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// デスクリプタヒープ
		RTVDescriptor* rtvDescriptor_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		// 描画レンダーテクスチャの情報
		RenderTarget renderTarget_{};
		ComPtr<ID3D12Resource> resource_;

		// GPUディスクリプタハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE uavGPUHandle_{};

		// 現在のリソース状態
		D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;
		// フォーマット
		DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
		// UAVを持っているかどうか
		bool hasUAV_ = false;

		// デスクリプタヒープのインデックス
		uint32_t rtvIndex_ = UINT32_MAX;
		uint32_t srvIndex_ = UINT32_MAX;
		uint32_t uavIndex_ = UINT32_MAX;
	};
} // Engine