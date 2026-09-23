#include "GraphicsFrameContext.h"

#include <Engine/Core/Rendering/DxObject/Descriptors/DxDescriptor.h>

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

		SetCurrentIndex(index);
		++frameSerial_;
	}

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
	batches_.push_back({ fenceValue, std::move(pending_) });
	pending_.clear();
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
