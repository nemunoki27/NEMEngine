#include "FrameUploadBufferAllocator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace Engine;

//============================================================================
//	FrameUploadBufferAllocator classMethods
//============================================================================
FrameUploadBufferAllocator::FrameUploadBufferAllocator(size_t initialCapacity) :
	initialCapacity_(AlignAllocation(initialCapacity)) {
}

FrameUploadBufferAllocator::~FrameUploadBufferAllocator() {

	Release();
}

FrameUploadBufferAllocator::FrameUploadBufferAllocator(FrameUploadBufferAllocator&& other) noexcept {

	Swap(other);
}

FrameUploadBufferAllocator& FrameUploadBufferAllocator::operator=(FrameUploadBufferAllocator&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void FrameUploadBufferAllocator::Swap(FrameUploadBufferAllocator& other) noexcept {

	std::swap(frameStates_, other.frameStates_);
	std::swap(retirement_, other.retirement_);
	std::swap(initialCapacity_, other.initialCapacity_);
}

void FrameUploadBufferAllocator::BeginFrame() {

	auto& state = frameStates_[GraphicsFrameState::GetCurrentIndex()];
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (state.frameSerial == serial) return;
	// 少量の利用が続いた領域だけ次の割当で縮小する
	if (state.capacity > initialCapacity_ && state.usedBytes <= state.capacity / 4) {
		if (state.lowUsageSerial == UINT64_MAX) state.lowUsageSerial = serial;
		if (HasExpiredGraphicsResource(state.lowUsageSerial, serial)) {
			state.shrinkCapacity = (std::max)(initialCapacity_, AlignAllocation(state.usedBytes * 2));
		}
	} else {
		state.lowUsageSerial = UINT64_MAX;
	}
	// 同じframeの別Viewでは使用領域を戻さない
	state.usedBytes = 0;
	state.offset = 0;
	state.frameSerial = serial;
}

void FrameUploadBufferAllocator::Release() {

	if (retirement_) retirement_->ReservePending(kGraphicsFrameContextCount);
	for (auto& state : frameStates_) {
		if (state.resource) {
			retirement_->Retire(state.resource);
			state.resource->Unmap(0, nullptr);
		}
		state = {};
	}
	retirement_ = nullptr;
}

FrameUploadBufferAllocation FrameUploadBufferAllocator::AllocateAndUploadBytes(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, std::span<const uint8_t> bytes) {

	if (bytes.empty()) return {};
	if (!device || (retirement_ && retirement_ != &retirement)) {
		throw std::logic_error("転送BufferのDeviceまたは回収先が不正です");
	}
	retirement_ = &retirement;
	BeginFrame();
	auto& state = frameStates_[GraphicsFrameState::GetCurrentIndex()];
	const size_t alignedSize = AlignAllocation(bytes.size());
	if (alignedSize > SIZE_MAX - state.usedBytes) throw std::length_error("frame内の転送使用量が多すぎます");
	if (state.shrinkCapacity) {
		EnsureCapacity(device, state, (std::max)(state.shrinkCapacity, alignedSize));
	}
	if (!state.resource || alignedSize > state.capacity - state.offset) {
		const size_t doubled = state.capacity <= SIZE_MAX / 2 ? state.capacity * 2 : state.capacity;
		EnsureCapacity(device, state, (std::max)({ initialCapacity_, doubled, alignedSize }));
	}

	// 公開済みアドレスとは別の領域へ書き込む
	const size_t offset = state.offset;
	std::memset(state.mappedData + offset, 0, alignedSize);
	std::memcpy(state.mappedData + offset, bytes.data(), bytes.size());
	state.offset += alignedSize;
	state.usedBytes += alignedSize;
	return { state.resource->GetGPUVirtualAddress() + offset, alignedSize };
}

void FrameUploadBufferAllocator::EnsureCapacity(ID3D12Device* device, FrameAllocationState& state, size_t requiredSize) {

	ComPtr<ID3D12Resource> resource;
	DxUtils::CreateUploadBufferResource(device, resource, requiredSize);
	void* mapped = nullptr;
	if (!DxDredDiagnostics::CheckHRESULT(device, resource->Map(0, nullptr, &mapped), "FrameUploadBufferAllocator::Map")) {
		throw std::runtime_error("転送BufferのMapに失敗しました");
	}

	// 作成成功後に旧領域を退避して差し替える
	if (state.resource) {
		retirement_->Retire(state.resource);
		state.resource->Unmap(0, nullptr);
	}
	state.resource = std::move(resource);
	state.mappedData = static_cast<uint8_t*>(mapped);
	state.capacity = requiredSize;
	state.lowUsageSerial = UINT64_MAX;
	state.shrinkCapacity = 0;
	state.offset = 0;
}

size_t FrameUploadBufferAllocator::AlignAllocation(size_t sizeInBytes) {

	constexpr size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	const size_t size = (std::max)(sizeInBytes, kAlignment);
	if (size > SIZE_MAX - (kAlignment - 1)) throw std::length_error("転送Bufferの容量が大きすぎます");
	return (size + kAlignment - 1) & ~(kAlignment - 1);
}
