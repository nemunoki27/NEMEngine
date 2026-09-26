#pragma once

//============================================================================
//	include
//============================================================================
#include "FrameUploadBufferAllocator.h"

// c++
#include <cstddef>
#include <cstdint>
#include <span>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	FrameConstantBufferAllocation structure
	//============================================================================
	struct FrameConstantBufferAllocation {

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		size_t sizeInBytes = 0;
	};

	//============================================================================
	//	FrameConstantBufferAllocator class
	// 描画ごとに定数領域を切り出し、GPU完了まで保持する
	//============================================================================
	class FrameConstantBufferAllocator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit FrameConstantBufferAllocator(size_t initialCapacity = 64 * 1024);
		~FrameConstantBufferAllocator() = default;
		FrameConstantBufferAllocator(const FrameConstantBufferAllocator&) = delete;
		FrameConstantBufferAllocator& operator=(const FrameConstantBufferAllocator&) = delete;
		FrameConstantBufferAllocator(FrameConstantBufferAllocator&& other) noexcept = default;
		FrameConstantBufferAllocator& operator=(FrameConstantBufferAllocator&& other) noexcept = default;

		// フレーム開始時に切り出し位置を戻す
		void BeginFrame();
		// UploadHeapを破棄する
		void Release();

		// 構造体をCBV用に転送してGPUアドレスを返す
		template<typename T>
		FrameConstantBufferAllocation AllocateAndUpload(GraphicsResourceRetirement& retirement, ID3D12Device* device, const T& data);
		// 可変長データをCBV用に転送してGPUアドレスを返す
		FrameConstantBufferAllocation AllocateAndUploadBytes(GraphicsResourceRetirement& retirement,
			ID3D12Device* device, std::span<const uint8_t> bytes);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		FrameUploadBufferAllocator allocator_;

	};

	//============================================================================
	//	FrameConstantBufferAllocator templateMethods
	//============================================================================
	template<typename T>
	FrameConstantBufferAllocation FrameConstantBufferAllocator::AllocateAndUpload(
		GraphicsResourceRetirement& retirement, ID3D12Device* device, const T& data) {

		return AllocateAndUploadBytes(retirement, device,
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(T)));
	}
} // Engine

