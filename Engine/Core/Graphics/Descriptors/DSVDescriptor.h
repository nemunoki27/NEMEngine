#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Descriptors/BaseDescriptor.h>

namespace Engine {

	//============================================================================
	//	DSVDescriptor class
	//	深度ステンシルビュー(DSV)ディスクリプタを管理し、作成/参照を提供する。
	//============================================================================
	class DSVDescriptor :
		public BaseDescriptor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DSVDescriptor() :BaseDescriptor(16) {};
		~DSVDescriptor() = default;

		// 深度リソースの作成
		void InitFrameBufferDSV(uint32_t width, uint32_t height);

		// 深度テクスチャからDSVを作成しCPUハンドルを割り当てる。
		void CreateDSV(uint32_t width, uint32_t height, uint32_t& index,
			D3D12_CPU_DESCRIPTOR_HANDLE& handle, ComPtr<ID3D12Resource>& resource,
			DXGI_FORMAT resourceFormat, DXGI_FORMAT depthClearFormat);

		//--------- accessor -----------------------------------------------------

		void SetFrameGPUHandle(const D3D12_GPU_DESCRIPTOR_HANDLE& handle) { dsvGPUHandle_ = handle; }

		ID3D12Resource* GetResource() const { return resource_.Get(); }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetFrameCPUHandle() const { return dsvCPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetFrameGPUHandle() const { return dsvGPUHandle_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle_;
		D3D12_GPU_DESCRIPTOR_HANDLE dsvGPUHandle_;

		//--------- functions ----------------------------------------------------

		// 深度リソースを確保し、クリア形式を設定する。
		void CreateDepthResource(ComPtr<ID3D12Resource>& resource, uint32_t width, uint32_t height,
			DXGI_FORMAT resourceFormat, DXGI_FORMAT depthClearFormat);
	};

}; // Engine
