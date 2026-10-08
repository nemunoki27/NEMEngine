#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	DxFrameMappedUploadBuffer class
	// フレームごとにCPU書き込み領域を分離する可変長Upload Heap
	//============================================================================
	class DxFrameMappedUploadBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxFrameMappedUploadBuffer() = default;
		~DxFrameMappedUploadBuffer();
		DxFrameMappedUploadBuffer(const DxFrameMappedUploadBuffer&) = delete;
		DxFrameMappedUploadBuffer& operator=(const DxFrameMappedUploadBuffer&) = delete;
		DxFrameMappedUploadBuffer(DxFrameMappedUploadBuffer&& other) noexcept;
		DxFrameMappedUploadBuffer& operator=(DxFrameMappedUploadBuffer&& other) noexcept;

		// 回収窓口を接続する
		void SetRetirementQueue(GraphicsResourceRetirement& queue);

		// 必要容量まで全フレーム分のバッファを拡張する
		bool EnsureCapacity(ID3D12Device* device, size_t requiredSize,
			std::string_view resourceName, size_t minimumCapacity = 256);
		// 現在フレームのマップ領域へ書き込む
		void Write(const void* data, size_t sizeInBytes, size_t offset = 0);
		// 保持リソースを解放する
		void Release();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const { return GetResource(GraphicsFrameState::GetCurrentIndex()); }
		ID3D12Resource* GetResource(uint32_t frameIndex) const { return resources_[frameIndex % kGraphicsFrameContextCount].Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;
		size_t GetCapacity() const { return capacity_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::array<ComPtr<ID3D12Resource>,
			kGraphicsFrameContextCount> resources_{};
		std::array<uint8_t*, kGraphicsFrameContextCount> mappedData_{};
		// 容量拡張前のリソースは使用中のGPUから切り離されるまで保持する
		GraphicsResourceRetirement* retirementQueue_ = nullptr;
		size_t capacity_ = 0;

		//--------- functions ----------------------------------------------------

		// 所有と参照を一組で交換する
		void Swap(DxFrameMappedUploadBuffer& other) noexcept;
	};

} // Engine
