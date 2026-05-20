#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Common/ComPtr.h>

// c++
#include <cstddef>
#include <cstdint>
#include <span>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	PostProcessParameterBuffer class
	//	可変サイズのPostProcessParametersを転送するための小さなCBVバッファ。
	//============================================================================
	class PostProcessParameterBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessParameterBuffer() = default;
		~PostProcessParameterBuffer() = default;

		// 指定サイズ以上のCBVバッファを確保する
		void Ensure(ID3D12Device* device, size_t sizeInBytes);
		// CPU側で組み立てた定数バッファ内容を転送する
		void Upload(std::span<const uint8_t> bytes);
		// バッファを破棄する
		void Release();

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return resource_ && mappedData_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_{};
		uint8_t* mappedData_ = nullptr;
		size_t capacity_ = 0;
	};
} // Engine
