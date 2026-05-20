#include "PostProcessParameterBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>

// c++
#include <algorithm>
#include <cassert>
#include <cstring>

//============================================================================
//	PostProcessParameterBuffer classMethods
//============================================================================

namespace {

	size_t AlignConstantBufferSize(size_t sizeInBytes) {

		constexpr size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		return (std::max<size_t>(sizeInBytes, kAlignment) + kAlignment - 1) & ~(kAlignment - 1);
	}
}

void Engine::PostProcessParameterBuffer::Ensure(ID3D12Device* device, size_t sizeInBytes) {

	const size_t requiredSize = AlignConstantBufferSize(sizeInBytes);
	if (resource_ && capacity_ >= requiredSize) {
		return;
	}

	Release();

	DxUtils::CreateBufferResource(device, resource_, requiredSize);

	void* mapped = nullptr;
	const HRESULT hr = resource_->Map(0, nullptr, &mapped);
	assert(SUCCEEDED(hr));

	mappedData_ = static_cast<uint8_t*>(mapped);
	capacity_ = requiredSize;
}

void Engine::PostProcessParameterBuffer::Upload(std::span<const uint8_t> bytes) {

	if (!mappedData_ || bytes.empty()) {
		return;
	}

	std::memset(mappedData_, 0, capacity_);
	std::memcpy(mappedData_, bytes.data(), (std::min)(capacity_, bytes.size()));
}

void Engine::PostProcessParameterBuffer::Release() {

	if (resource_ && mappedData_) {
		resource_->Unmap(0, nullptr);
	}
	resource_.Reset();
	mappedData_ = nullptr;
	capacity_ = 0;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::PostProcessParameterBuffer::GetGPUAddress() const {

	return resource_ ? resource_->GetGPUVirtualAddress() : 0;
}
