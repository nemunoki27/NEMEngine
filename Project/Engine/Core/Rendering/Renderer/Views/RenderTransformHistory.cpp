#include "RenderTransformHistory.h"

//============================================================================
//	RenderTransformHistory classMethods
//============================================================================
size_t Engine::RenderTransformHistory::InstanceKeyHash::operator()(const InstanceKey& key) const {

	return WorldEntityKeyHash{}(key.owner) ^ (std::hash<uint64_t>{}(key.instanceID) << 1);
}

Engine::Matrix4x4 Engine::RenderTransformHistory::Update(const std::string& view, WorldEntityKey owner,
	uint64_t instanceID, uint64_t frameSerial, const Matrix4x4& current) {

	Collect(frameSerial);
	auto& frame = views_[view][{owner, instanceID}];
	if (!frame.initialized || frame.serial != frameSerial) {
		frame.previous = frame.initialized && frame.serial + 1 == frameSerial ? frame.current : current;
		frame.current = current;
		frame.serial = frameSerial;
		frame.initialized = true;
	}
	return frame.previous;
}

void Engine::RenderTransformHistory::Collect(uint64_t frameSerial) {

	if (collectedFrame_ == frameSerial) return;
	for (auto& [view, instances] : views_) {
		std::erase_if(instances, [frameSerial](const auto& entry) { return entry.second.serial + 2 < frameSerial; });
	}
	std::erase_if(views_, [](const auto& entry) { return entry.second.empty(); });
	collectedFrame_ = frameSerial;
}
