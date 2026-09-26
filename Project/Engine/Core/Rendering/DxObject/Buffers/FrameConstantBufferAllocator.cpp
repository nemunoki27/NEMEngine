#include "FrameConstantBufferAllocator.h"

//============================================================================
//	include
//============================================================================
#include <stdexcept>

using namespace Engine;

//============================================================================
//	FrameConstantBufferAllocator classMethods
//============================================================================
FrameConstantBufferAllocator::FrameConstantBufferAllocator(size_t initialCapacity) :
	allocator_(initialCapacity) {
}

void FrameConstantBufferAllocator::BeginFrame() {

	allocator_.BeginFrame();
}

void FrameConstantBufferAllocator::Release() {

	allocator_.Release();
}

FrameConstantBufferAllocation FrameConstantBufferAllocator::AllocateAndUploadBytes(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, std::span<const uint8_t> bytes) {

	// CBVの上限を確認して共通領域へ転送する
	constexpr size_t kMaxConstantBytes = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
	if (bytes.size() > kMaxConstantBytes) throw std::length_error("定数Bufferの上限を超えています");
	const auto allocation = allocator_.AllocateAndUploadBytes(retirement, device, bytes);
	return { allocation.gpuAddress, allocation.sizeInBytes };
}
