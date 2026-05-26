#include "ComponentTypeRegistry.h"

//============================================================================
//	ComponentTypeRegistry classMethods
//============================================================================

const Engine::ComponentTypeInfo& Engine::ComponentTypeRegistry::GetInfo(uint32_t id) const {

	assert(id < infos_.size());
	return infos_[id];
}

const Engine::ComponentTypeInfo* Engine::ComponentTypeRegistry::FindByName(const std::string_view& name) const {

	auto it = nameToID_.find(std::string(name));
	if (it == nameToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

Engine::ComponentTypeRegistry& Engine::ComponentTypeRegistry::GetInstance() {

	static ComponentTypeRegistry registry;
	return registry;
}