#include "PostProcessConstantBufferAllocator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>

// c++
#include <algorithm>
#include <cassert>
#include <cstring>

//============================================================================
//	PostProcessConstantBufferAllocator classMethods
//============================================================================

namespace {

	constexpr size_t kInitialCapacity = 64 * 1024;
}

void Engine::PostProcessConstantBufferAllocator::BeginFrame() {

	// 同一フレーム内では上書きしない。フレーム先頭でのみ再利用する。
	offset_ = 0;
	retiredResources_.clear();
}

void Engine::PostProcessConstantBufferAllocator::Release() {

	if (resource_ && mappedData_) {
		resource_->Unmap(0, nullptr);
	}
	resource_.Reset();
	retiredResources_.clear();
	mappedData_ = nullptr;
	capacity_ = 0;
	offset_ = 0;
}

Engine::PostProcessConstantBufferAllocation Engine::PostProcessConstantBufferAllocator::AllocateAndUploadBytes(
	ID3D12Device* device, std::span<const uint8_t> bytes) {

	if (!device || bytes.empty()) {
		return {};
	}

	const size_t alignedSize = AlignCBV(bytes.size());
	if (!resource_ || offset_ + alignedSize > capacity_) {

		// 既にDispatchへ渡したアドレスを壊さないよう、古いUploadHeapはフレーム内だけ保持する。
		const size_t growSize = (std::max)(capacity_ * 2, offset_ + alignedSize);
		EnsureCapacity(device, (std::max)(growSize, kInitialCapacity));
	}
	if (!mappedData_ || !resource_) {
		return {};
	}

	const size_t writeOffset = offset_;
	std::memset(mappedData_ + writeOffset, 0, alignedSize);
	std::memcpy(mappedData_ + writeOffset, bytes.data(), bytes.size());
	offset_ += alignedSize;

	PostProcessConstantBufferAllocation allocation{};
	allocation.gpuAddress = resource_->GetGPUVirtualAddress() + writeOffset;
	allocation.sizeInBytes = alignedSize;
	return allocation;
}

void Engine::PostProcessConstantBufferAllocator::EnsureCapacity(ID3D12Device* device, size_t requiredSize) {

	requiredSize = AlignCBV(requiredSize);
	if (resource_ && capacity_ >= requiredSize && offset_ == 0) {
		return;
	}

	if (resource_ && mappedData_) {
		resource_->Unmap(0, nullptr);
		retiredResources_.emplace_back(std::move(resource_));
	}

	DxUtils::CreateBufferResource(device, resource_, requiredSize);

	void* mapped = nullptr;
	const HRESULT hr = resource_->Map(0, nullptr, &mapped);
	assert(SUCCEEDED(hr));

	mappedData_ = static_cast<uint8_t*>(mapped);
	capacity_ = requiredSize;
	offset_ = 0;
}

size_t Engine::PostProcessConstantBufferAllocator::AlignCBV(size_t sizeInBytes) {

	constexpr size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	return ((std::max)(sizeInBytes, kAlignment) + kAlignment - 1) & ~(kAlignment - 1);
}
