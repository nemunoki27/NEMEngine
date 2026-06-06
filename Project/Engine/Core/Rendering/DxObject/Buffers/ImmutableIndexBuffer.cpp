#include "ImmutableIndexBuffer.h"

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

	const UINT sizeInBytes = static_cast<UINT>(sizeof(uint32_t) * data.size());

	// DEFAULT heapのバッファは作成時COMMON。コピー用のCOPY_DEST遷移はBufferUploadServiceで積む
	DxUtils::CreateDefaultBufferResource(device, resource_, sizeInBytes);

	indexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
	indexBufferView_.Format = format;
	indexBufferView_.SizeInBytes = sizeInBytes;

	uploadService.EnqueueBufferUpload(resource_.Get(), std::as_bytes(data), finalState);
}

void ImmutableIndexBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
	std::span<const uint16_t> data, D3D12_RESOURCE_STATES finalState) {

	if (data.empty()) {
		return;
	}

	const UINT sizeInBytes = static_cast<UINT>(sizeof(uint16_t) * data.size());

	// DEFAULT heapのバッファは作成時COMMON。コピー用のCOPY_DEST遷移はBufferUploadServiceで積む
	DxUtils::CreateDefaultBufferResource(device, resource_, sizeInBytes);

	indexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
	indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
	indexBufferView_.SizeInBytes = sizeInBytes;

	uploadService.EnqueueBufferUpload(resource_.Get(), std::as_bytes(data), finalState);
}
