#include "IrisTransitionComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

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

	void QueueCommand(Engine::IrisTransitionComponent& component,
		Engine::IrisTransitionCommand command, float value = 0.0f) {

		component.runtimeCommand = command;
		component.runtimeCommandValue = value;
		component.runtimeCommandSerial = NextCommandSerial();
	}
}

//============================================================================
//	IrisTransitionComponent structMethods
//============================================================================
void Engine::IrisTransitionComponent::IrisOut() {

	if (!enabled) {
		return;
	}
	QueueCommand(*this, IrisTransitionCommand::IrisOut);
}

void Engine::IrisTransitionComponent::IrisIn() {

	if (!enabled) {
		return;
	}
	QueueCommand(*this, IrisTransitionCommand::IrisIn);
}

void Engine::IrisTransitionComponent::SetProgress(float progress) {

	QueueCommand(*this, IrisTransitionCommand::SetProgress,
		std::clamp(progress, 0.0f, 1.0f));
}

void Engine::IrisTransitionComponent::Cancel() {

	QueueCommand(*this, IrisTransitionCommand::Cancel);
}

void Engine::IrisTransitionComponent::Reset() {

	QueueCommand(*this, IrisTransitionCommand::Reset);
}

//============================================================================
//	IrisTransitionComponent classMethods
//============================================================================
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

void Engine::ResetIrisTransitionRuntime(IrisTransitionComponent& component) {

	component.runtimeState = IrisTransitionState::Open;
	component.runtimeProgress = 0.0f;
	component.runtimeCommand = IrisTransitionCommand::None;
	component.runtimeCommandValue = 0.0f;
	component.runtimeCommandSerial = 0;
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
	ResetIrisTransitionRuntime(component);
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
