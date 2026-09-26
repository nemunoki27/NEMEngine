#include "ImmutableIndexBuffer.h"

// c++
#include <stdexcept>

using namespace Engine;

//============================================================================
//	ImmutableIndexBuffer classMethods
//============================================================================
void ImmutableIndexBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
	std::span<const uint32_t> data, DXGI_FORMAT format, D3D12_RESOURCE_STATES finalState) {

	// 空配列はバッファを作らない
	if (data.empty()) {
		return;
	}

	if (format != DXGI_FORMAT_R32_UINT) throw std::invalid_argument("32bit IndexにはR32_UINT形式が必要です");
	if (data.size() > UINT32_MAX / sizeof(uint32_t)) throw std::length_error("Index Bufferの容量が大きすぎます");
	const UINT sizeInBytes = static_cast<UINT>(sizeof(uint32_t) * data.size());

	buffer_.Create(device, uploadService, std::as_bytes(data), finalState);

	indexBufferView_.BufferLocation = buffer_.GetGPUVirtualAddress();
	indexBufferView_.Format = format;
	indexBufferView_.SizeInBytes = sizeInBytes;
}

void ImmutableIndexBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
	std::span<const uint16_t> data, D3D12_RESOURCE_STATES finalState) {

	if (data.empty()) {
		return;
	}

	if (data.size() > UINT32_MAX / sizeof(uint16_t)) throw std::length_error("Index Bufferの容量が大きすぎます");
	const UINT sizeInBytes = static_cast<UINT>(sizeof(uint16_t) * data.size());

	buffer_.Create(device, uploadService, std::as_bytes(data), finalState);

	indexBufferView_.BufferLocation = buffer_.GetGPUVirtualAddress();
	indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
	indexBufferView_.SizeInBytes = sizeInBytes;
}

//============================================================================
//	ImmutableIndexBuffer classMethods
//============================================================================

namespace Engine {

	uint32_t ImmutableIndexBuffer::GetIndexSizeInBytes() const {

		return (indexBufferView_.Format == DXGI_FORMAT_R16_UINT) ? sizeof(uint16_t) : sizeof(uint32_t);
	}
}
