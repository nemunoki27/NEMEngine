#include "PostProcessConstantBufferAllocator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <algorithm>
#include <cstring>

//============================================================================
//	PostProcessConstantBufferAllocator classMethods
//============================================================================
namespace {

	constexpr size_t kInitialCapacity = 64 * 1024;
}

void Engine::PostProcessConstantBufferAllocator::BeginFrame() {

	FrameAllocationState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	// Fence完了済みのフレーム領域だけを先頭から再利用する
	state.offset = 0;
	state.retiredResources.clear();
}

void Engine::PostProcessConstantBufferAllocator::Release() {

	for (FrameAllocationState& state : frameStates_) {
		if (state.resource && state.mappedData) {
			state.resource->Unmap(0, nullptr);
		}
		state.resource.Reset();
		state.retiredResources.clear();
		state.mappedData = nullptr;
		state.capacity = 0;
		state.offset = 0;
	}
}

Engine::PostProcessConstantBufferAllocation Engine::PostProcessConstantBufferAllocator::AllocateAndUploadBytes(
	ID3D12Device* device, std::span<const uint8_t> bytes) {

	if (!device || bytes.empty()) {
		return {};
	}

	FrameAllocationState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	const size_t alignedSize = AlignCBV(bytes.size());
	if (!state.resource || state.offset + alignedSize > state.capacity) {

		// 既にDispatchへ渡したアドレスを壊さないよう、古いUploadHeapはフレーム内だけ保持する
		const size_t growSize = (std::max)(
			state.capacity * 2, state.offset + alignedSize);
		EnsureCapacity(device, state,
			(std::max)(growSize, kInitialCapacity));
	}
	if (!state.mappedData || !state.resource) {
		return {};
	}

	const size_t writeOffset = state.offset;
	std::memset(state.mappedData + writeOffset, 0, alignedSize);
	std::memcpy(state.mappedData + writeOffset, bytes.data(), bytes.size());
	state.offset += alignedSize;

	PostProcessConstantBufferAllocation allocation{};
	allocation.gpuAddress =
		state.resource->GetGPUVirtualAddress() + writeOffset;
	allocation.sizeInBytes = alignedSize;
	return allocation;
}

void Engine::PostProcessConstantBufferAllocator::EnsureCapacity(
	ID3D12Device* device, FrameAllocationState& state, size_t requiredSize) {

	requiredSize = AlignCBV(requiredSize);
	if (state.resource && state.capacity >= requiredSize && state.offset == 0) {
		return;
	}

	if (state.resource && state.mappedData) {
		state.resource->Unmap(0, nullptr);
		state.retiredResources.emplace_back(std::move(state.resource));
	}

	DxUtils::CreateBufferResource(device, state.resource, requiredSize);

	void* mapped = nullptr;
	const HRESULT hr = state.resource->Map(0, nullptr, &mapped);
	Assert::Call(SUCCEEDED(hr), "ポストプロセス定数バッファのMapに失敗しました");

	state.mappedData = static_cast<uint8_t*>(mapped);
	state.capacity = requiredSize;
	state.offset = 0;
}

size_t Engine::PostProcessConstantBufferAllocator::AlignCBV(size_t sizeInBytes) {

	constexpr size_t kAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	return ((std::max)(sizeInBytes, kAlignment) + kAlignment - 1) & ~(kAlignment - 1);
}
