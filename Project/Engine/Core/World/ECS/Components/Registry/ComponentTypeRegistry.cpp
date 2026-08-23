#include "ComponentTypeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Generated/BuiltinComponentRegistry.generated.h>

//============================================================================
//	ComponentTypeRegistry classMethods
//============================================================================
Engine::ComponentTypeRegistry::ComponentTypeRegistry() {

	RegisterBuiltinComponents(*this);
}

const Engine::ComponentTypeInfo& Engine::ComponentTypeRegistry::GetInfo(uint32_t id) const {

	Assert::Call(id < infos_.size(), "未登録のComponentType IDです");
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
