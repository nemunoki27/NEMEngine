#include "ScriptRuntimeValueCache.h"

#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

void Engine::ScriptRuntimeValueCache::BeginFrame(bool playing) {

	now_ = std::chrono::steady_clock::now();
	if (!playing && !values_.empty()) {
		values_.clear();
	}
	while (values_.size() > kMaximumEntries) {
		auto oldest = values_.begin();
		for (auto it = values_.begin(); it != values_.end(); ++it) {
			if (it->second.first < oldest->second.first) {
				oldest = it;
			}
		}
		values_.erase(oldest);
	}
}

nlohmann::json& Engine::ScriptRuntimeValueCache::GetState(BehaviorHandle handle) {

	const uint64_t key = (static_cast<uint64_t>(handle.index) << 32) | handle.generation;
	auto& cached = values_[key];
	if (cached.second.is_null() || (now_ - cached.first) > std::chrono::milliseconds(100)) {
		cached.first = now_;
		cached.second = BehaviorSystem::GetRuntimeSerializedState(handle);
	}
	if (!cached.second.is_object()) {
		cached.second = nlohmann::json::object();
	}
	return cached.second;
}
