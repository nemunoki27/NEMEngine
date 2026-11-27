#include "FrameUploadBufferAllocator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>

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
	frameRegistration_.Swap(other.frameRegistration_, this, &other);
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
	frameRegistration_.Reset();
}

FrameUploadBufferAllocation FrameUploadBufferAllocator::AllocateAndUploadBytes(
	GraphicsResourceRetirement& retirement, ID3D12Device* device, std::span<const uint8_t> bytes) {

	if (bytes.empty()) return {};
	const auto region = Reserve(retirement, device, bytes.size());
	std::memcpy(region.mapped, bytes.data(), bytes.size());
	std::memset(region.mapped + bytes.size(), 0, region.size - bytes.size());
	return {region.resource->GetGPUVirtualAddress() + region.offset, region.size};
}

FrameUploadBufferRegion FrameUploadBufferAllocator::Reserve(GraphicsResourceRetirement& retirement,
	ID3D12Device* device, size_t size, size_t alignment) {

	if (!size) return {};
	if (alignment != D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT && alignment != D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT)
		throw std::invalid_argument("転送Bufferのアラインメントが不正です");
	if (!device || (retirement_ && retirement_ != &retirement)) {
		throw std::logic_error("転送BufferのDeviceまたは回収先が不正です");
	}
	retirement_ = &retirement;
	frameRegistration_.Bind(retirement.GetFrameResources(), this, [](void* owner, uint32_t count) {
		static_cast<FrameUploadBufferAllocator*>(owner)->ReleaseInactiveFrames(count);
	});
	BeginFrame();
	auto& state = frameStates_[GraphicsFrameState::GetCurrentIndex()];
	const size_t alignedSize = AlignAllocation(size);
	if (alignedSize > SIZE_MAX - state.usedBytes) throw std::length_error("frame内の転送使用量が多すぎます");
	if (state.shrinkCapacity) {
		EnsureCapacity(device, state, (std::max)(state.shrinkCapacity, alignedSize));
	}
	const size_t padding = (alignment - (state.offset % alignment)) % alignment;
	if (!state.resource || padding > state.capacity - state.offset || alignedSize > state.capacity - state.offset - padding) {
		const size_t doubled = state.capacity <= SIZE_MAX / 2 ? state.capacity * 2 : state.capacity;
		EnsureCapacity(device, state, (std::max)({ initialCapacity_, doubled, alignedSize }));
	} else state.offset += padding;

	// 公開済みアドレスとは別の領域へ書き込む
	const size_t offset = state.offset;
	state.offset += alignedSize;
	state.usedBytes += alignedSize;
	return {state.resource.Get(), state.mappedData + offset, offset, alignedSize};
}

void FrameUploadBufferAllocator::EnsureCapacity(ID3D12Device* device, FrameAllocationState& state, size_t requiredSize) {

	ComPtr<ID3D12Resource> resource;
	DxUtils::CreateUploadBufferResource(device, resource, requiredSize);
	void* mapped = nullptr;
	if (!DxDREDDiagnostics::CheckHRESULT(device, resource->Map(0, nullptr, &mapped), "FrameUploadBufferAllocator::Map")) {
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

void Engine::FrameUploadBufferAllocator::ReleaseInactiveFrames(uint32_t count) {

	for (uint32_t index = count; index < kGraphicsFrameContextCount; ++index) {
		auto& state = frameStates_[index];
		if (state.resource) {
			state.resource->Unmap(0, nullptr);
			retirement_->Retire(std::move(state.resource));
		}
		state = {};
	}
}
