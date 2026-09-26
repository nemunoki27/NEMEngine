#include "DxMappedUploadBuffer.h"

#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <stdexcept>

//============================================================================
//	DxMappedUploadBuffer classMethods
//============================================================================

namespace Engine {

	void DxMappedUploadBuffer::Create(GraphicsResourceRetirement& retirement, ID3D12Device* device, size_t sizeInBytes) {

		if (retirement_ && retirement_ != &retirement) throw std::logic_error("Upload Bufferの回収先は変更できません");
		ComPtr<ID3D12Resource> candidate;
		DxUtils::CreateUploadBufferResource(device, candidate, sizeInBytes);
		void* mapped = nullptr;
		if (!DxDredDiagnostics::CheckHRESULT(device, candidate->Map(0, nullptr, &mapped), "DxMappedUploadBuffer::Map")) {
			throw std::runtime_error("Upload BufferのMapに失敗しました");
		}
		// 再生成に成功するまで旧Map先を維持する
		if (resource_) retirement.Retire(resource_);
		if (resource_ && mappedData_) resource_->Unmap(0, nullptr);
		retirement_ = &retirement;
		resource_ = std::move(candidate);
		mappedData_ = static_cast<std::byte*>(mapped);
		capacityInBytes_ = sizeInBytes;
		isCreated_ = true;
	}

	void DxMappedUploadBuffer::Write(const void* src, size_t sizeInBytes, size_t dstOffset) {

		// 未マップや空データは何もしない
		if (!mappedData_ || src == nullptr || sizeInBytes == 0) {
			return;
		}

		// 桁あふれを避けて転送範囲を確認する
		if (dstOffset > capacityInBytes_ || sizeInBytes > capacityInBytes_ - dstOffset) {
			throw std::out_of_range("Upload Bufferの書き込みが容量を超えています");
		}

		std::memcpy(mappedData_ + dstOffset, src, sizeInBytes);
	}
}

Engine::DxMappedUploadBuffer::~DxMappedUploadBuffer() {

	Release();
}

Engine::DxMappedUploadBuffer::DxMappedUploadBuffer(DxMappedUploadBuffer&& other) noexcept {

	Swap(other);
}

Engine::DxMappedUploadBuffer& Engine::DxMappedUploadBuffer::operator=(DxMappedUploadBuffer&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void Engine::DxMappedUploadBuffer::Release() {

	// 使用中のResourceを返してMap参照を外す
	if (resource_) retirement_->Retire(std::move(resource_));
	mappedData_ = nullptr;
	capacityInBytes_ = 0;
	isCreated_ = false;
	retirement_ = nullptr;
}

void Engine::DxMappedUploadBuffer::Swap(DxMappedUploadBuffer& other) noexcept {

	std::swap(resource_, other.resource_);
	std::swap(retirement_, other.retirement_);
	std::swap(mappedData_, other.mappedData_);
	std::swap(capacityInBytes_, other.capacityInBytes_);
	std::swap(isCreated_, other.isCreated_);
}
