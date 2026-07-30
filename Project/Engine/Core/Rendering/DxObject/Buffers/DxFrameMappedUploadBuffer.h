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
		~DxFrameMappedUploadBuffer() = default;

		// 必要容量まで全フレーム分のバッファを拡張する
		bool EnsureCapacity(ID3D12Device* device, size_t requiredSize,
			std::string_view resourceName, size_t minimumCapacity = 256);
		// 現在フレームのマップ領域へ書き込む
		void Write(const void* data, size_t sizeInBytes, size_t offset = 0);
		// 保持リソースを解放する
		void Release();

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const {
			return GetResource(GraphicsFrameState::GetCurrentIndex());
		}
		ID3D12Resource* GetResource(uint32_t frameIndex) const {
			return resources_[frameIndex % kGraphicsFrameContextCount].Get();
		}
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			ID3D12Resource* resource = GetResource();
			return resource ? resource->GetGPUVirtualAddress() : 0;
		}
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
		GraphicsDeferredReleaseQueue retiredResources_{};
		size_t capacity_ = 0;
	};

	inline bool DxFrameMappedUploadBuffer::EnsureCapacity(
		ID3D12Device* device, size_t requiredSize,
		std::string_view resourceName, size_t minimumCapacity) {

		requiredSize = (std::max)(requiredSize, minimumCapacity);
		if (requiredSize <= capacity_) {
			return false;
		}

		size_t newCapacity = (std::max)(capacity_, minimumCapacity);
		while (newCapacity < requiredSize) {
			newCapacity *= 2;
		}

		for (uint32_t frameIndex = 0;
			frameIndex < kGraphicsFrameContextCount; ++frameIndex) {

			if (resources_[frameIndex]) {
				retiredResources_.Retire(std::move(resources_[frameIndex]));
			}
			mappedData_[frameIndex] = nullptr;
			DxUtils::CreateBufferResource(
				device, resources_[frameIndex], newCapacity);
			const HRESULT hr = resources_[frameIndex]->Map(0, nullptr,
				reinterpret_cast<void**>(&mappedData_[frameIndex]));
			Assert::Call(SUCCEEDED(hr),
				"DxFrameMappedUploadBufferのMapに失敗しました");
			if (!resourceName.empty()) {
				const std::string name = std::string(resourceName) +
					"[" + std::to_string(frameIndex) + "]";
				resources_[frameIndex]->SetName(
					Algorithm::ConvertString(name).c_str());
			}
		}
		capacity_ = newCapacity;
		return true;
	}

	inline void DxFrameMappedUploadBuffer::Write(
		const void* data, size_t sizeInBytes, size_t offset) {

		retiredResources_.Collect();
		if (!data || sizeInBytes == 0) {
			return;
		}
		Assert::Call(offset + sizeInBytes <= capacity_,
			"DxFrameMappedUploadBufferの書き込みが容量を超えています");
		uint8_t* mapped =
			mappedData_[GraphicsFrameState::GetCurrentIndex()];
		Assert::Call(mapped != nullptr,
			"DxFrameMappedUploadBufferが作成されていません");
		std::memcpy(mapped + offset, data, sizeInBytes);
	}

	inline void DxFrameMappedUploadBuffer::Release() {

		resources_ = {};
		mappedData_ = {};
		retiredResources_.Clear();
		capacity_ = 0;
	}
} // Engine
