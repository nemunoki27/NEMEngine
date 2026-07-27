#include "IrisTransitionComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	IrisTransitionComponent internal
//============================================================================
namespace {

	uint64_t NextCommandSerial() {

		static uint64_t serial = 1;
		const uint64_t result = serial++;
		if (serial == 0) {
			serial = 1;
		}
		return result;
	}

	void QueueCommand(Engine::IrisTransitionRuntimeComponent& runtime,
		Engine::IrisTransitionCommand command, float value = 0.0f) {

		runtime.command = command;
		runtime.commandValue = value;
		runtime.commandSerial = NextCommandSerial();
	}
}

//============================================================================
//	IrisTransitionComponent classMethods
//============================================================================
void Engine::IrisTransitionComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] IrisTransitionComponent& component) {

	if (!world.HasComponent<IrisTransitionRuntimeComponent>(entity)) {
		world.AddComponent<IrisTransitionRuntimeComponent>(entity);
	}
}

void Engine::IrisTransitionComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<IrisTransitionRuntimeComponent>(entity)) {
		world.RemoveComponent<IrisTransitionRuntimeComponent>(entity);
	}
}

void Engine::IrisTransitionComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] IrisTransitionComponent& component) {
}

void Engine::IrisTransitionComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] IrisTransitionComponent& component) {
}

void Engine::IrisTransitionComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, IrisTransitionComponent& component) {

	from_json(in, component);
}

void Engine::IrisTransitionComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const IrisTransitionComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

void Engine::ApplyIrisTransitionAuthoring(const IrisTransitionComponent& source,
	IrisTransitionComponent& destination) {

	destination.enabled = source.enabled;
	destination.screenPosition = source.screenPosition;
	destination.transitionColor = source.transitionColor;
	destination.edgeSoftness = source.edgeSoftness;
	destination.invertMask = source.invertMask;
	destination.irisOutDuration = source.irisOutDuration;
	destination.irisOutEasing = source.irisOutEasing;
	destination.irisInDuration = source.irisInDuration;
	destination.irisInEasing = source.irisInEasing;
	destination.blockInput = source.blockInput;
	destination.autoIrisInAfterSceneTransition =
		source.autoIrisInAfterSceneTransition;
	destination.useUnscaledTime = source.useUnscaledTime;
	destination.previewInEditMode = source.previewInEditMode;
	destination.previewProgress = source.previewProgress;
}

void Engine::RequestIrisOut(
	ECSWorld& world, const Entity& entity, float progress) {

	const IrisTransitionComponent* component =
		world.TryGetComponent<IrisTransitionComponent>(entity);
	IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity);
	if (component && component->enabled && runtime) {
		QueueCommand(*runtime, IrisTransitionCommand::IrisOut,
			std::clamp(progress, 0.0f, 1.0f));
	}
}

void Engine::RequestIrisIn(
	ECSWorld& world, const Entity& entity, float progress) {

	const IrisTransitionComponent* component =
		world.TryGetComponent<IrisTransitionComponent>(entity);
	IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity);
	if (component && component->enabled && runtime) {
		QueueCommand(*runtime, IrisTransitionCommand::IrisIn,
			std::clamp(progress, 0.0f, 1.0f));
	}
}

void Engine::RequestIrisProgress(
	ECSWorld& world, const Entity& entity, float progress) {

	if (IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity)) {
		QueueCommand(*runtime, IrisTransitionCommand::SetProgress,
			std::clamp(progress, 0.0f, 1.0f));
	}
}

void Engine::RequestIrisCancel(
	ECSWorld& world, const Entity& entity) {

	if (IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity)) {
		QueueCommand(*runtime, IrisTransitionCommand::Cancel);
	}
}

void Engine::RequestIrisReset(
	ECSWorld& world, const Entity& entity) {

	if (IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity)) {
		QueueCommand(*runtime, IrisTransitionCommand::Reset);
	}
}

void Engine::RequestIrisEditPreview(
	ECSWorld& world, const Entity& entity) {

	if (IrisTransitionRuntimeComponent* runtime =
		world.TryGetComponent<IrisTransitionRuntimeComponent>(entity)) {
		runtime->editPreviewSerial = NextCommandSerial();
	}
}

void Engine::ResetIrisTransitionRuntime(
	IrisTransitionRuntimeComponent& runtime) {

	runtime = {};
}

void Engine::from_json(const nlohmann::json& in, IrisTransitionComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	if (const auto position = in.find("screenPosition"); position != in.end()) {
		component.screenPosition = Vector2::FromJson(*position);
	}
	if (const auto color = in.find("transitionColor"); color != in.end()) {
		component.transitionColor = Color4::FromJson(*color);
	}
	component.edgeSoftness = (std::max)(in.value("edgeSoftness", component.edgeSoftness), 0.0f);
	component.invertMask = in.value("invertMask", component.invertMask);
	component.irisOutDuration = (std::max)(
		in.value("irisOutDuration", component.irisOutDuration), 0.0f);
	component.irisOutEasing = EnumAdapter<EasingType>::FromString(
		in.value("irisOutEasing", "EaseInOutSine")).value_or(component.irisOutEasing);
	component.irisInDuration = (std::max)(
		in.value("irisInDuration", component.irisInDuration), 0.0f);
	component.irisInEasing = EnumAdapter<EasingType>::FromString(
		in.value("irisInEasing", "EaseInOutSine")).value_or(component.irisInEasing);
	component.blockInput = in.value("blockInput", component.blockInput);
	component.autoIrisInAfterSceneTransition = in.value(
		"autoIrisInAfterSceneTransition", component.autoIrisInAfterSceneTransition);
	component.useUnscaledTime = in.value("useUnscaledTime", component.useUnscaledTime);
	component.previewInEditMode = in.value("previewInEditMode", component.previewInEditMode);
	component.previewProgress = std::clamp(
		in.value("previewProgress", component.previewProgress), 0.0f, 1.0f);
}

void Engine::to_json(nlohmann::json& out, const IrisTransitionComponent& component) {

	out["enabled"] = component.enabled;
	out["screenPosition"] = component.screenPosition.ToJson();
	out["transitionColor"] = component.transitionColor.ToJson();
	out["edgeSoftness"] = component.edgeSoftness;
	out["invertMask"] = component.invertMask;
	out["irisOutDuration"] = component.irisOutDuration;
	out["irisOutEasing"] = EnumAdapter<EasingType>::ToString(component.irisOutEasing);
	out["irisInDuration"] = component.irisInDuration;
	out["irisInEasing"] = EnumAdapter<EasingType>::ToString(component.irisInEasing);
	out["blockInput"] = component.blockInput;
	out["autoIrisInAfterSceneTransition"] =
		component.autoIrisInAfterSceneTransition;
	out["useUnscaledTime"] = component.useUnscaledTime;
	out["previewInEditMode"] = component.previewInEditMode;
	out["previewProgress"] = component.previewProgress;
}
