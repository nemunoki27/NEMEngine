#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstdint>
#include <utility>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	AccelerationStructureBuffer class
	//	DXR用バッファ
	//============================================================================
	class AccelerationStructureBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AccelerationStructureBuffer() = default;
		~AccelerationStructureBuffer() = default;

		// リソース作成
		void Create(ID3D12Device* device, UINT64 sizeInBytes, D3D12_RESOURCE_FLAGS flags,
			D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT);

		// このBufferへのGPUアクセスを同期する
		void InsertUAVBarrier(ID3D12GraphicsCommandList* commandList) const;

		// リセット
		void Reset() { resource_.Reset(); }
		// 所有リソースを退避用に移動する
		ComPtr<ID3D12Resource> TakeResource() { return std::move(resource_); }

		// valueをASサイズにアラインメントする
		static constexpr UINT64 AlignASSize(UINT64 value);

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const { return resource_.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return resource_->GetGPUVirtualAddress(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
	};
} // Engine
