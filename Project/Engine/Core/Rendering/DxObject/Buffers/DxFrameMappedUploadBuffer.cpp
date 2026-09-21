#include "DxFrameMappedUploadBuffer.h"

//============================================================================
//	DxFrameMappedUploadBuffer classMethods
//============================================================================

namespace Engine {

	D3D12_GPU_VIRTUAL_ADDRESS DxFrameMappedUploadBuffer::GetGPUAddress() const {

		ID3D12Resource* resource = GetResource();
		return resource ? resource->GetGPUVirtualAddress() : 0;
	}

	bool DxFrameMappedUploadBuffer::EnsureCapacity(
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

	void DxFrameMappedUploadBuffer::Write(
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

	void DxFrameMappedUploadBuffer::Release() {

		resources_ = {};
		mappedData_ = {};
		retiredResources_.Clear();
		capacity_ = 0;
	}
}
