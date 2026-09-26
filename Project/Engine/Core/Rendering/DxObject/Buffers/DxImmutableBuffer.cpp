#include "DxImmutableBuffer.h"

// c++
#include <stdexcept>

//============================================================================
//	DxImmutableBuffer classMethods
//============================================================================

namespace Engine {

	void DxImmutableBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
		std::span<const std::byte> data, D3D12_RESOURCE_STATES finalState) {

		// 空配列はバッファを作らない(Buffer Widthが0にならないようにする)
		if (data.empty()) {
			return;
		}

		auto& retirement = uploadService.GetResourceRetirement();
		if (retirement_ && retirement_ != &retirement) throw std::logic_error("静的Bufferの回収先は変更できません");
		ComPtr<ID3D12Resource> candidate;
		DxUtils::CreateDefaultBufferResource(device, candidate, data.size());
		uploadService.EnqueueBufferUpload(candidate.Get(), data, finalState);
		// 転送要求が成立してから旧Resourceを差し替える
		if (resource_) retirement.Retire(resource_);
		retirement_ = &retirement;
		resource_ = std::move(candidate);
	}
}

Engine::DxImmutableBuffer::~DxImmutableBuffer() {

	Release();
}

Engine::DxImmutableBuffer::DxImmutableBuffer(DxImmutableBuffer&& other) noexcept {

	Swap(other);
}

Engine::DxImmutableBuffer& Engine::DxImmutableBuffer::operator=(DxImmutableBuffer&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void Engine::DxImmutableBuffer::Release() {

	// 転送中の保持とは別に、描画完了まで残す
	if (resource_) retirement_->Retire(std::move(resource_));
	retirement_ = nullptr;
}

void Engine::DxImmutableBuffer::Swap(DxImmutableBuffer& other) noexcept {

	std::swap(resource_, other.resource_);
	std::swap(retirement_, other.retirement_);
}
