#include "GraphicsFrameContext.h"

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

	void GraphicsDeferredReleaseQueue::Retire(ComPtr<ID3D12Resource> resource) {

		if (!resource) {
			return;
		}
		Collect();
		resources_[GraphicsFrameState::GetCurrentIndex()].emplace_back(
			std::move(resource));
	}

	void GraphicsDeferredReleaseQueue::Collect() {

		const uint32_t frameIndex =
			GraphicsFrameState::GetCurrentIndex();
		const uint64_t frameSerial =
			GraphicsFrameState::GetFrameSerial();
		if (frameSerials_[frameIndex] == frameSerial) {
			return;
		}

		resources_[frameIndex].clear();
		frameSerials_[frameIndex] = frameSerial;
	}

	void GraphicsDeferredReleaseQueue::Clear() {

		resources_ = {};
		frameSerials_ = {};
	}
}
