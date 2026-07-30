#include "CameraShakeComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	CameraShakeComponent structMethods
//============================================================================
void Engine::CameraShakeComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] CameraShakeComponent& component) {

	if (!world.HasComponent<CameraShakeRuntimeComponent>(entity)) {
		world.AddComponent<CameraShakeRuntimeComponent>(entity);
	}
}

void Engine::CameraShakeComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<CameraShakeRuntimeComponent>(entity)) {
		world.RemoveComponent<CameraShakeRuntimeComponent>(entity);
	}
}

void Engine::CameraShakeComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CameraShakeComponent& component) {
}

void Engine::CameraShakeComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CameraShakeComponent& component) {
}

void Engine::CameraShakeComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, CameraShakeComponent& component) {

	from_json(in, component);
}

void Engine::CameraShakeComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const CameraShakeComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

void Engine::from_json(const nlohmann::json& in, CameraShakeComponent& component) {

	component.duration = in.value("duration", component.duration);
	component.easingType = EnumAdapter<EasingType>::FromString(in.value("easingType", "Linear")).value_or(component.easingType);
	component.strength = Vector3::FromJson(in.value("strength", nlohmann::json()));
}

void Engine::to_json(nlohmann::json& out, const CameraShakeComponent& component) {

	out["duration"] = component.duration;
	out["easingType"] = EnumAdapter<EasingType>::ToString(component.easingType);
	out["strength"] = component.strength.ToJson();
}
