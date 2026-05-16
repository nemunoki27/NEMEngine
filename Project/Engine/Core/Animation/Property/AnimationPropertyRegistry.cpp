#include "AnimationPropertyRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	AnimationPropertyRegistry classMethods
//============================================================================

Engine::AnimationPropertyRegistry& Engine::AnimationPropertyRegistry::GetInstance() {

	static AnimationPropertyRegistry registry;
	return registry;
}

void Engine::AnimationPropertyRegistry::Register(const AnimationPropertyDescriptor& desc) {

	if (desc.componentName.empty() || desc.propertyPath.empty()) {
		return;
	}

	// 同じComponent.Propertyは上書き登録しない。
	// Builtin登録を複数回呼んでも、ツール側のAddProperty一覧が重複しないようにしている。
	if (Find(desc.componentName, desc.propertyPath)) {
		return;
	}
	properties_.emplace_back(desc);
}

const Engine::AnimationPropertyDescriptor* Engine::AnimationPropertyRegistry::Find(
	std::string_view componentName, std::string_view propertyPath) const {

	auto it = std::find_if(properties_.begin(), properties_.end(),
		[componentName, propertyPath](const AnimationPropertyDescriptor& desc) {
			return desc.componentName == componentName && desc.propertyPath == propertyPath;
		});
	return it != properties_.end() ? &(*it) : nullptr;
}

std::vector<const Engine::AnimationPropertyDescriptor*> Engine::AnimationPropertyRegistry::GetPropertiesForEntity(
	ECSWorld& world, const Entity& entity) const {

	std::vector<const AnimationPropertyDescriptor*> result{};
	if (!world.IsAlive(entity)) {
		return result;
	}

	for (const AnimationPropertyDescriptor& desc : properties_) {
		if (!desc.hasComponent || !desc.hasComponent(world, entity)) {
			continue;
		}
		result.emplace_back(&desc);
	}
	return result;
}
