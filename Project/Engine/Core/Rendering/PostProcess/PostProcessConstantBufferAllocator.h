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
	//	PostProcessConstantBufferAllocation structure
	//============================================================================
	struct PostProcessConstantBufferAllocation {

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		size_t sizeInBytes = 0;
	};

	//============================================================================
	//	PostProcessConstantBufferAllocator class
	// PostProcessのDispatchごとに別のCBV領域を切り出すUploadAllocator
	//============================================================================
	class PostProcessConstantBufferAllocator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PostProcessConstantBufferAllocator() = default;
		~PostProcessConstantBufferAllocator() = default;

		// フレーム開始時に切り出し位置を戻す
		void BeginFrame();
		// UploadHeapを破棄する
		void Release();

		// 構造体をCBV用に転送してGPUアドレスを返す
		template<typename T>
		PostProcessConstantBufferAllocation AllocateAndUpload(ID3D12Device* device, const T& data);
		// 可変長データをCBV用に転送してGPUアドレスを返す
		PostProcessConstantBufferAllocation AllocateAndUploadBytes(ID3D12Device* device, std::span<const uint8_t> bytes);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		struct FrameAllocationState {

			ComPtr<ID3D12Resource> resource{};
			std::vector<ComPtr<ID3D12Resource>> retiredResources{};
			uint8_t* mappedData = nullptr;
			size_t capacity = 0;
			size_t offset = 0;
		};
		std::array<FrameAllocationState, kGraphicsFrameContextCount> frameStates_{};

		//--------- functions ----------------------------------------------------

		void EnsureCapacity(ID3D12Device* device,
			FrameAllocationState& state, size_t requiredSize);
		static size_t AlignCBV(size_t sizeInBytes);
	};

	//============================================================================
	//	PostProcessConstantBufferAllocator templateMethods
	//============================================================================
	template<typename T>
	PostProcessConstantBufferAllocation PostProcessConstantBufferAllocator::AllocateAndUpload(
		ID3D12Device* device, const T& data) {

		return AllocateAndUploadBytes(device,
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(T)));
	}
} // Engine

