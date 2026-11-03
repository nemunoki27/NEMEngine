#include "SimpleAnimationComponent.h"

// c++
//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <limits>
#include <type_traits>

void Engine::ApplySimpleAnimationSettings(const SimpleAnimationComponent& source, SimpleAnimationComponent& destination) {

	destination.timeSource = source.timeSource;
	destination.enabled = source.enabled;
	destination.playOnStart = source.playOnStart;
	destination.playInEditMode = source.playInEditMode;
	destination.autoApply = source.autoApply;
	destination.target = source.target;
	destination.binding = source.binding;
	destination.settings = source.settings;
}

void Engine::PublishSimpleAnimationValue(SimpleAnimationComponent& component) {

	const auto& runtime = component.runtime;
	component.runtimeValid = runtime.sample.valid;
	component.runtimePlaying = runtime.playing && !runtime.paused && component.enabled;
	component.runtimePaused = runtime.paused;
	component.runtimeFinished = runtime.sample.finished;
	component.runtimeTime = static_cast<float>(runtime.time);
	component.runtimeCycles = static_cast<int32_t>((std::min)(runtime.sample.completedCycles, uint64_t(INT32_MAX)));
	component.runtimeType = static_cast<int32_t>(runtime.sample.value.index());
	std::visit([&](const auto& value) {
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, int32_t>) component.runtimeInteger = value;
		else if constexpr (std::is_same_v<T, float>) component.runtimeValue = Vector4(value, 0, 0, 0);
		else if constexpr (std::is_same_v<T, Vector2>) component.runtimeValue = Vector4(value.x, value.y, 0, 0);
		else if constexpr (std::is_same_v<T, Vector3>) component.runtimeValue = Vector4(value.x, value.y, value.z, 0);
		else if constexpr (std::is_same_v<T, Color3>) component.runtimeValue = Vector4(value.r, value.g, value.b, 1);
		else if constexpr (std::is_same_v<T, Color4>) component.runtimeValue = Vector4(value.r, value.g, value.b, value.a);
		else component.runtimeValue = Vector4(value.x, value.y, value.z, value.w);
	}, runtime.sample.value);
}

void Engine::from_json(const nlohmann::json& in, SimpleAnimationComponent& component) {

	SimpleAnimationComponent candidate;
	candidate.timeSource = in.value("timeSource", component.timeSource);
	PreserveTimeSourceRuntime(component.timeSource, candidate.timeSource);
	candidate.enabled = in.value("enabled", true);
	candidate.playOnStart = in.value("playOnStart", true);
	candidate.playInEditMode = in.value("playInEditMode", false);
	candidate.autoApply = in.value("autoApply", false);
	if (in.contains("target")) candidate.target = in.at("target").get<SceneEntityReference>();
	if (in.contains("binding")) candidate.binding = in.at("binding").get<AnimationPropertyBinding>();
	if (in.contains("settings")) candidate.settings = in.at("settings").get<SimpleAnimationSettings>();
	if (!in.contains("timeSource") && candidate.settings.unscaledTime) candidate.timeSource.mode = TimeSourceMode::Unscaled;
	ApplySimpleAnimationSettings(candidate, component);
}

void Engine::to_json(nlohmann::json& out, const SimpleAnimationComponent& component) {

	out = {{"timeSource", component.timeSource}, {"enabled", component.enabled}, {"playOnStart", component.playOnStart},
		{"playInEditMode", component.playInEditMode}, {"autoApply", component.autoApply},
		{"target", component.target}, {"binding", component.binding}, {"settings", component.settings}};
}
