#include "DxFrameMappedUploadBuffer.h"

#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <stdexcept>

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

		if (!retirementQueue_) throw std::logic_error("Upload Bufferの回収窓口が設定されていません");
		requiredSize = (std::max)(requiredSize, minimumCapacity);
		if (requiredSize <= capacity_) {
			return false;
		}

		size_t newCapacity = (std::max)({ capacity_, minimumCapacity, size_t{ 1 } });
		while (newCapacity < requiredSize) {
			newCapacity = newCapacity > SIZE_MAX / 2 ? requiredSize : newCapacity * 2;
		}
		std::array<ComPtr<ID3D12Resource>, kGraphicsFrameContextCount> resources;
		std::array<uint8_t*, kGraphicsFrameContextCount> mapped{};
		// 全frameの確保とMapを済ませてから公開する
		for (uint32_t index = 0; index < kGraphicsFrameContextCount; ++index) {
			DxUtils::CreateUploadBufferResource(device, resources[index], newCapacity);
			const HRESULT result = resources[index]->Map(0, nullptr, reinterpret_cast<void**>(&mapped[index]));
			if (!DxDredDiagnostics::CheckHRESULT(device, result, "DxFrameMappedUploadBuffer::Map")) {
				throw std::runtime_error("Upload BufferのMapに失敗しました");
			}
			if (!resourceName.empty()) {
				const std::string name = std::string(resourceName) + "[" + std::to_string(index) + "]";
				resources[index]->SetName(Algorithm::ConvertString(name).c_str());
			}
		}
		retirementQueue_->ReservePending(kGraphicsFrameContextCount);
		for (auto& resource : resources_) {
			if (resource) retirementQueue_->Retire(resource);
		}
		for (auto& resource : resources_) {
			if (resource) resource->Unmap(0, nullptr);
		}
		resources_ = std::move(resources);
		mappedData_ = mapped;
		capacity_ = newCapacity;
		return true;
	}

	void DxFrameMappedUploadBuffer::Write(
		const void* data, size_t sizeInBytes, size_t offset) {

		if (!data || sizeInBytes == 0) {
			return;
		}
		if (offset > capacity_ || sizeInBytes > capacity_ - offset) {
			throw std::out_of_range("Upload Bufferの書き込みが容量を超えています");
		}
		uint8_t* mapped =
			mappedData_[GraphicsFrameState::GetCurrentIndex()];
		if (!mapped) throw std::logic_error("Upload Bufferが作成されていません");
		std::memcpy(mapped + offset, data, sizeInBytes);
	}

	void DxFrameMappedUploadBuffer::Release() {

		for (auto& resource : resources_) {
			if (resource) retirementQueue_->Retire(std::move(resource));
		}
		mappedData_ = {};
		capacity_ = 0;
	}
}

Engine::DxFrameMappedUploadBuffer::~DxFrameMappedUploadBuffer() {

	Release();
}

Engine::DxFrameMappedUploadBuffer::DxFrameMappedUploadBuffer(DxFrameMappedUploadBuffer&& other) noexcept {

	Swap(other);
}

Engine::DxFrameMappedUploadBuffer& Engine::DxFrameMappedUploadBuffer::operator=(
	DxFrameMappedUploadBuffer&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void Engine::DxFrameMappedUploadBuffer::Swap(DxFrameMappedUploadBuffer& other) noexcept {

	std::swap(resources_, other.resources_);
	std::swap(mappedData_, other.mappedData_);
	std::swap(retirementQueue_, other.retirementQueue_);
	std::swap(capacity_, other.capacity_);
}

void Engine::DxFrameMappedUploadBuffer::SetRetirementQueue(GraphicsResourceRetirement& queue) {

	if (capacity_ != 0 && retirementQueue_ != &queue) throw std::logic_error("使用中のUpload Bufferの回収窓口は変更できません");
	retirementQueue_ = &queue;
}
