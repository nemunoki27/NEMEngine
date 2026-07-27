#include "SkinnedAnimationComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SkinnedAnimationComponent classMethods
//============================================================================
void Engine::SkinnedAnimationRuntimeComponent::OnAdded(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	SkinnedAnimationRuntimeComponent& component) {

	// 複製や復元で無効化されたハンドルも必ず現在のWorldへ張り直す
	if (!component.handle.IsValid()) {
		component.handle =
			world.GetStorage().Get<SkinnedAnimationRuntimeStorage>().Emplace();
	}
}

void Engine::SkinnedAnimationRuntimeComponent::InitializeStorage(
	ECSWorld& world, const Entity& entity,
	SkinnedAnimationRuntimeComponent& component) {

	OnAdded(world, entity, component);
}

void Engine::SkinnedAnimationRuntimeComponent::ReleaseStorage(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	SkinnedAnimationRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		return;
	}

	world.GetStorage().Get<SkinnedAnimationRuntimeStorage>().Release(
		component.handle);
	component.handle = SkinnedAnimationRuntimeHandle::Null();
}

void Engine::SkinnedAnimationRuntimeComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] SkinnedAnimationRuntimeComponent& component) {
}

void Engine::SkinnedAnimationRuntimeComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const SkinnedAnimationRuntimeComponent& component,
	nlohmann::json& out) {

	out = nlohmann::json::object();
}

void Engine::SkinnedAnimationComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] SkinnedAnimationComponent& component) {

	// 設定コンポーネントとRuntimeハンドルを常に対で維持する
	if (!world.HasComponent<SkinnedAnimationRuntimeComponent>(entity)) {
		world.AddComponent<SkinnedAnimationRuntimeComponent>(entity);
	}
}

void Engine::SkinnedAnimationComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<SkinnedAnimationRuntimeComponent>(entity)) {
		world.RemoveComponent<SkinnedAnimationRuntimeComponent>(entity);
	}
}

void Engine::SkinnedAnimationComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] SkinnedAnimationComponent& component) {
}

void Engine::SkinnedAnimationComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] SkinnedAnimationComponent& component) {
}

void Engine::SkinnedAnimationComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, SkinnedAnimationComponent& component) {

	from_json(in, component);
}

void Engine::SkinnedAnimationComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const SkinnedAnimationComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

Engine::SkinnedAnimationRuntimeData* Engine::TryGetSkinnedAnimationRuntime(
	ECSWorld& world, const Entity& entity) {

	SkinnedAnimationRuntimeComponent* runtime =
		world.TryGetComponent<SkinnedAnimationRuntimeComponent>(entity);
	if (!runtime) {
		return nullptr;
	}
	return world.GetStorage().Get<SkinnedAnimationRuntimeStorage>().TryGet(
		runtime->handle);
}

const Engine::SkinnedAnimationRuntimeData* Engine::TryGetSkinnedAnimationRuntime(
	const ECSWorld& world, const Entity& entity) {

	const SkinnedAnimationRuntimeComponent* runtime =
		world.TryGetComponent<SkinnedAnimationRuntimeComponent>(entity);
	const SkinnedAnimationRuntimeStorage* storage =
		world.GetStorage().TryGet<SkinnedAnimationRuntimeStorage>();
	return runtime && storage ? storage->TryGet(runtime->handle) : nullptr;
}

void Engine::from_json(const nlohmann::json& in, SkinnedAnimationComponent& component) {

	component.enabled = in.value("enabled", true);
	component.loop = in.value("loop", true);
	component.playInEditMode = in.value("playInEditMode", true);
	component.playbackSpeed = in.value("playbackSpeed", 1.0f);
	component.transitionDuration = in.value("transitionDuration", 0.15f);
	component.clip = in.value("clip", std::string("Default"));
	component.isDisplayBone = in.value("isDisplayBone", false);
}

void Engine::to_json(nlohmann::json& out, const SkinnedAnimationComponent& component) {

	out["enabled"] = component.enabled;
	out["loop"] = component.loop;
	out["playInEditMode"] = component.playInEditMode;
	out["playbackSpeed"] = component.playbackSpeed;
	out["transitionDuration"] = component.transitionDuration;
	out["clip"] = component.clip;
	out["isDisplayBone"] = component.isDisplayBone;
}
