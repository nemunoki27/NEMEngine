#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	FrameUploadBufferAllocation structure
	//============================================================================
	struct FrameUploadBufferAllocation {

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		size_t sizeInBytes = 0;
	};

	//============================================================================
	//	FrameUploadBufferAllocator class
	// 転送ごとに領域を切り出し、GPU完了まで保持する
	//============================================================================
	class FrameUploadBufferAllocator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit FrameUploadBufferAllocator(size_t initialCapacity = 64 * 1024);
		~FrameUploadBufferAllocator();
		FrameUploadBufferAllocator(const FrameUploadBufferAllocator&) = delete;
		FrameUploadBufferAllocator& operator=(const FrameUploadBufferAllocator&) = delete;
		FrameUploadBufferAllocator(FrameUploadBufferAllocator&& other) noexcept;
		FrameUploadBufferAllocator& operator=(FrameUploadBufferAllocator&& other) noexcept;

		// フレーム開始時に切り出し位置を戻す
		void BeginFrame();
		// UploadHeapを破棄する
		void Release();

		// 構造体をGPU用に転送してGPUアドレスを返す
		template<typename T>
		FrameUploadBufferAllocation AllocateAndUpload(GraphicsResourceRetirement& retirement, ID3D12Device* device, const T& data);
		// 可変長データをGPU用に転送してGPUアドレスを返す
		FrameUploadBufferAllocation AllocateAndUploadBytes(GraphicsResourceRetirement& retirement,
			ID3D12Device* device, std::span<const uint8_t> bytes);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct FrameAllocationState {

			ComPtr<ID3D12Resource> resource{};
			uint8_t* mappedData = nullptr;
			size_t capacity = 0;
			size_t offset = 0;
			size_t usedBytes = 0;
			size_t shrinkCapacity = 0;
			uint64_t lowUsageSerial = UINT64_MAX;
			uint64_t frameSerial = UINT64_MAX;
		};
		//--------- variables ----------------------------------------------------

		std::array<FrameAllocationState, kGraphicsFrameContextCount> frameStates_{};
		GraphicsResourceRetirement* retirement_ = nullptr;
		size_t initialCapacity_ = 0;

		//--------- functions ----------------------------------------------------

		// 成功した領域だけを現在の書き込み先にする
		void EnsureCapacity(ID3D12Device* device,
			FrameAllocationState& state, size_t requiredSize);
		// 転送領域の配置境界へ切り上げる
		static size_t AlignAllocation(size_t sizeInBytes);
		// 所有情報をまとめて交換する
		void Swap(FrameUploadBufferAllocator& other) noexcept;
	};

	//============================================================================
	//	FrameUploadBufferAllocator templateMethods
	//============================================================================
	template<typename T>
	FrameUploadBufferAllocation FrameUploadBufferAllocator::AllocateAndUpload(
		GraphicsResourceRetirement& retirement, ID3D12Device* device, const T& data) {

		return AllocateAndUploadBytes(retirement, device,
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(T)));
	}
} // Engine

