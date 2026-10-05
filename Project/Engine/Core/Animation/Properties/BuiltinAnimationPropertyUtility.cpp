#include "BuiltinAnimationPropertyUtility.h"

void Engine::AnimationPropertyUtility::Register(Engine::AnimationPropertyRegistry& registry, const char* componentName,
	const char* propertyPath, const char* displayName, Engine::AnimationValueType valueType,
	bool (*hasComponent)(Engine::ECSWorld&, const Engine::Entity&),
	bool (*getValue)(Engine::ECSWorld&, const Engine::Entity&, Engine::AnimationPropertyValue&),
	bool (*setValue)(Engine::ECSWorld&, const Engine::Entity&, const Engine::AnimationPropertyValue&)) {

	// ToolとRuntimeの両方から同じDescriptorを引けるようにする
	Engine::AnimationPropertyDescriptor desc{};
	desc.componentName = componentName;
	desc.propertyPath = propertyPath;
	desc.displayName = displayName;
	desc.valueType = valueType;
	desc.hasComponent = hasComponent;
	desc.getValue = getValue;
	desc.setValue = setValue;
	registry.Register(desc);
}
