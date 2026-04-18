#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// c++
#include <cstdint>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	BaseDescriptor structures
	//============================================================================

	// デスクリプタの種類
	struct DescriptorType {

		D3D12_DESCRIPTOR_HEAP_TYPE heapType;
		D3D12_DESCRIPTOR_HEAP_FLAGS heapFlags;
	};
	enum class DescriptorHeapType {

		SRV,
		RTV,
		DSV
	};

	//============================================================================
	//	BaseDescriptor class
	//	ディスクリプタヒープの生成/インデックス管理の共通基盤を提供する。
	//============================================================================
	class BaseDescriptor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BaseDescriptor(uint32_t maxDescriptorCount);
		virtual ~BaseDescriptor() = default;

		void Init(ID3D12Device* device, const DescriptorType& descriptorType);

		// 空きスロットを確保
		uint32_t Allocate();
		// 確保済みスロットを解放
		void Free(uint32_t index);
		//--------- accessor -----------------------------------------------------

		D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const;

		ID3D12DescriptorHeap* GetDescriptorHeap() const { return descriptorHeap_.Get(); }

		bool IsAllocated(uint32_t index) const;

		// 現在生きているデスクリプタ数
		uint32_t GetUseDescriptorCount() const { return allocatedCount_; }
		// これまでに到達した最大インデックス+1
		uint32_t GetHighWaterMark() const { return useIndex_; }
		uint32_t GetMaxDescriptorCount() const { return maxDescriptorCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint32_t descriptorSize_;
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_;

		uint32_t maxDescriptorCount_;
		// 未使用の新規領域の先頭
		uint32_t useIndex_ = 0;
		// 現在確保中のデスクリプタ数
		uint32_t allocatedCount_ = 0;

		// 再利用可能なインデックスのリスト
		std::vector<uint32_t> freeList_;
		// インデックスごとの使用中フラグ
		std::vector<uint8_t> allocationFlags_;

		ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
	};
}; // Engine