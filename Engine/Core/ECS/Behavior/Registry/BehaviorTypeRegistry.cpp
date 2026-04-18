#include "BehaviorTypeRegistry.h"

//============================================================================
//	BehaviorTypeRegistry classMethods
//============================================================================

const Engine::BehaviorTypeInfo& Engine::BehaviorTypeRegistry::GetInfo(uint32_t id) const {

	assert(id < infos_.size());
	return infos_[id];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindByName(const std::string_view& name) const {

	auto it = nameToID_.find(std::string(name));
	if (it == nameToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

Engine::BehaviorTypeRegistry& Engine::BehaviorTypeRegistry::GetInstance() {

	static BehaviorTypeRegistry registry;
	return registry;
}