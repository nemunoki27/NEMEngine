#include "GraphicsFrameContext.h"

#include <Engine/Core/Rendering/DxObject/Descriptors/DxDescriptor.h>
#include <stdexcept>

//============================================================================
//	GraphicsFrameContext classMethods
//============================================================================

namespace Engine {

	void GraphicsFrameState::SetActiveCount(uint32_t count) {

		activeCount_ = count < 1 ? 1 :
			(count > kGraphicsFrameContextCount ?
				kGraphicsFrameContextCount : count);
		currentIndex_ %= activeCount_;
	}

	void GraphicsFrameState::BeginFrame(uint32_t index) {

		if (frameSerial_ >= UINT64_MAX - 1) throw std::overflow_error("描画frameの世代が上限に達しました");
		SetCurrentIndex(index);
		++frameSerial_;
	}

}

bool Engine::HasExpiredGraphicsResource(uint64_t lastUsedSerial, uint64_t frameSerial) {

	return lastUsedSerial != UINT64_MAX && frameSerial >= lastUsedSerial &&
		frameSerial - lastUsedSerial >= kGraphicsResourceReuseFrames;
}

void Engine::GraphicsResourceRetirement::ReservePending(size_t additionalCount) {

	if (additionalCount > pending_.max_size() - pending_.size()) throw std::length_error("GPU回収候補が多すぎます");
	pending_.reserve(pending_.size() + additionalCount);
}

void Engine::GraphicsResourceRetirement::Retire(ComPtr<ID3D12Object> object,
	BaseDescriptor* descriptor, uint32_t index) {

	if (!object && !descriptor) {
		return;
	}
	pending_.push_back({ std::move(object), descriptor, index });
	++pendingCount_;
}

void Engine::GraphicsResourceRetirement::Seal(uint64_t fenceValue) {

	if (pending_.empty() || fenceValue == 0) {
		return;
	}
	// 保持先の確保に失敗しても候補の所有を失わない
	batches_.emplace_back();
	batches_.back().fenceValue = fenceValue;
	batches_.back().entries.swap(pending_);
}

void Engine::GraphicsResourceRetirement::Collect(uint64_t completedFenceValue) {

	// Device Lostの値を正常な完了として扱わない
	if (completedFenceValue == UINT64_MAX) {
		return;
	}
	while (!batches_.empty() && batches_.front().fenceValue <= completedFenceValue) {
		for (const Entry& entry : batches_.front().entries) {
			if (entry.descriptor) {
				entry.descriptor->Free(entry.index);
			}
		}
		pendingCount_ -= batches_.front().entries.size();
		batches_.pop_front();
	}
}

void Engine::GraphicsResourceRetirement::ReleaseAfterDeviceRemoval(ID3D12Device* device) {

	if (!device || SUCCEEDED(device->GetDeviceRemovedReason())) {
		throw std::logic_error("稼働中のDeviceの資源は強制回収できません");
	}
	// Descriptor管理元が生きている間に番号を返す
	for (const Entry& entry : pending_) {
		if (entry.descriptor) entry.descriptor->Free(entry.index);
	}
	for (const Batch& batch : batches_) {
		for (const Entry& entry : batch.entries) {
			if (entry.descriptor) entry.descriptor->Free(entry.index);
		}
	}
	pending_.clear();
	batches_.clear();
	pendingCount_ = 0;
}
