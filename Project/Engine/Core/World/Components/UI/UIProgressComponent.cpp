#include "UIProgressComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	UIProgressComponent classMethods
//============================================================================
void Engine::UIProgressRuntimeComponent::OnAdded(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	UIProgressRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		component.handle =
			world.GetStorage().Get<UIProgressRuntimeStorage>().Emplace();
	}
}

void Engine::UIProgressRuntimeComponent::InitializeStorage(
	ECSWorld& world, const Entity& entity,
	UIProgressRuntimeComponent& component) {

	OnAdded(world, entity, component);
}

void Engine::UIProgressRuntimeComponent::ReleaseStorage(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	UIProgressRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		return;
	}
	world.GetStorage().Get<UIProgressRuntimeStorage>().Release(
		component.handle);
	component.handle = UIProgressRuntimeHandle::Null();
}

void Engine::UIProgressRuntimeComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] UIProgressRuntimeComponent& component) {
}

void Engine::UIProgressRuntimeComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const UIProgressRuntimeComponent& component,
	nlohmann::json& out) {

	out = nlohmann::json::object();
}

void Engine::UIProgressComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UIProgressComponent& component) {

	if (!world.HasComponent<UIProgressRuntimeComponent>(entity)) {
		world.AddComponent<UIProgressRuntimeComponent>(entity);
	}
}

void Engine::UIProgressComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<UIProgressRuntimeComponent>(entity)) {
		world.RemoveComponent<UIProgressRuntimeComponent>(entity);
	}
}

void Engine::UIProgressComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UIProgressComponent& component) {
}

void Engine::UIProgressComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UIProgressComponent& component) {
}

void Engine::UIProgressComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, UIProgressComponent& component) {

	from_json(in, component);
}

void Engine::UIProgressComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UIProgressComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

Engine::UIProgressRuntimeData* Engine::TryGetUIProgressRuntime(
	ECSWorld& world, const Entity& entity) {

	UIProgressRuntimeComponent* runtime =
		world.TryGetComponent<UIProgressRuntimeComponent>(entity);
	if (!runtime) {
		return nullptr;
	}
	return world.GetStorage().Get<UIProgressRuntimeStorage>().TryGet(
		runtime->handle);
}

const Engine::UIProgressRuntimeData* Engine::TryGetUIProgressRuntime(
	const ECSWorld& world, const Entity& entity) {

	const UIProgressRuntimeComponent* runtime =
		world.TryGetComponent<UIProgressRuntimeComponent>(entity);
	const UIProgressRuntimeStorage* storage =
		world.GetStorage().TryGet<UIProgressRuntimeStorage>();
	return runtime && storage ? storage->TryGet(runtime->handle) : nullptr;
}

void Engine::ApplyUIProgressAuthoring(const UIProgressComponent& source,
	UIProgressComponent& destination) {

	destination.enabled = source.enabled;
	destination.previewInEditMode = source.previewInEditMode;
	destination.minValue = source.minValue;
	destination.maxValue = source.maxValue;
	destination.value = source.value;
	destination.delayedTargetLocalFileID = source.delayedTargetLocalFileID;
	destination.direction = source.direction;
	destination.smooth = source.smooth;
	destination.smoothDuration = source.smoothDuration;
	destination.smoothEasing = source.smoothEasing;
	destination.delayed = source.delayed;
	destination.delayedTexture = source.delayedTexture;
	destination.delayedWait = source.delayedWait;
	destination.delayedDuration = source.delayedDuration;
	destination.delayedEasing = source.delayedEasing;
	destination.useUnscaledTime = source.useUnscaledTime;
}

void Engine::from_json(const nlohmann::json& in, UIProgressComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.previewInEditMode = in.value("previewInEditMode", component.previewInEditMode);
	component.minValue = in.value("minValue", component.minValue);
	component.maxValue = in.value("maxValue", component.maxValue);
	component.value = in.value("value", component.value);
	component.delayedTargetLocalFileID = UIComponentSerialization::ReadEntityReference(in, "delayedTarget");
	component.direction = EnumAdapter<UIProgressFillDirection>::FromString(
		in.value("direction", "LeftToRight")).value_or(component.direction);
	component.smooth = in.value("smooth", component.smooth);
	component.smoothDuration = in.value("smoothDuration", component.smoothDuration);
	component.smoothEasing = EnumAdapter<EasingType>::FromString(
		in.value("smoothEasing", "EaseOutSine")).value_or(component.smoothEasing);
	component.delayed = in.value("delayed", component.delayed);
	component.delayedTexture = ParseAssetID(in, "delayedTexture");
	component.delayedWait = in.value("delayedWait", component.delayedWait);
	component.delayedDuration = in.value("delayedDuration", component.delayedDuration);
	component.delayedEasing = EnumAdapter<EasingType>::FromString(
		in.value("delayedEasing", "EaseOutSine")).value_or(component.delayedEasing);
	component.useUnscaledTime = in.value("useUnscaledTime", component.useUnscaledTime);
}

void Engine::to_json(nlohmann::json& out, const UIProgressComponent& component) {

	out["enabled"] = component.enabled;
	out["previewInEditMode"] = component.previewInEditMode;
	out["minValue"] = component.minValue;
	out["maxValue"] = component.maxValue;
	out["value"] = component.value;
	out["delayedTarget"] = UIComponentSerialization::WriteEntityReference(component.delayedTargetLocalFileID);
	out["direction"] = EnumAdapter<UIProgressFillDirection>::ToString(component.direction);
	out["smooth"] = component.smooth;
	out["smoothDuration"] = component.smoothDuration;
	out["smoothEasing"] = EnumAdapter<EasingType>::ToString(component.smoothEasing);
	out["delayed"] = component.delayed;
	out["delayedTexture"] = ToAssetReferenceJson(component.delayedTexture);
	out["delayedWait"] = component.delayedWait;
	out["delayedDuration"] = component.delayedDuration;
	out["delayedEasing"] = EnumAdapter<EasingType>::ToString(component.delayedEasing);
	out["useUnscaledTime"] = component.useUnscaledTime;
}

void Engine::ResetUIProgressRuntime(
	UIProgressRuntimeData& runtime, float value) {

	runtime.displayedValue = value;
	runtime.delayedValue = value;
	runtime.displayStart = value;
	runtime.delayedStart = value;
	runtime.targetValue = value;
	runtime.smoothElapsed = 0.0f;
	runtime.delayedElapsed = 0.0f;
	runtime.fillTarget = {};
	runtime.delayedTarget = {};
	runtime.initialized = false;
}
