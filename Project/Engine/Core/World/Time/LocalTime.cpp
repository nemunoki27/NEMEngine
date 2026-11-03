#include "LocalTime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

Engine::Entity Engine::ResolveTimeSourceEntity(const ECSWorld& world, Entity owner, const TimeSourceSettings& settings) {

	return settings.runtimeSourceID ? world.FindByUUID(settings.runtimeSourceID) : settings.source.Resolve(world, owner);
}

void Engine::PreserveTimeSourceRuntime(const TimeSourceSettings& previous, TimeSourceSettings& next) {

	if (!next.runtimeSourceID && previous.mode == next.mode &&
		previous.source.sourceAsset == next.source.sourceAsset && previous.source.localFileID == next.source.localFileID &&
		previous.source.prefab == next.source.prefab) next.runtimeSourceID = previous.runtimeSourceID;
}

Engine::ResolvedLocalTime Engine::ResolveLocalTime(const ECSWorld& world, Entity owner,
	const TimeSourceSettings& settings, TimeChannel channel, float scaledDelta, float unscaledDelta) {

	if (!world.IsAlive(owner) || world.IsPendingDestroy(owner)) return {0.0f, 0.0f, false};
	auto sanitizeDelta = [](float value) { return std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f; };
	if (settings.mode == TimeSourceMode::Unscaled) return {sanitizeDelta(unscaledDelta), 1.0f, true};
	if (settings.mode == TimeSourceMode::Global) return {sanitizeDelta(scaledDelta), 1.0f, true};
	Entity current = settings.mode == TimeSourceMode::Explicit ? ResolveTimeSourceEntity(world, owner, settings) : owner;
	if (!world.IsAlive(current) || world.IsPendingDestroy(current) || (settings.mode == TimeSourceMode::Explicit &&
		!world.TryGetComponentForBinding<TimeScaleComponent>(current))) return {0.0f, 0.0f, false};
	double scale = 1.0;
	bool first = true;
	// 不正な親子循環でも時間解決を停止できるよう上限を持たせる
	for (uint32_t depth = 0; world.IsAlive(current); ++depth) {
		if (depth >= 1024 || world.IsPendingDestroy(current)) return {0.0f, 0.0f, false};
		if (const auto* component = world.TryGetComponentForBinding<TimeScaleComponent>(current)) {
			if (component->enabled && (first || component->applyToChildren)) {
				if ((component->channels & static_cast<uint32_t>(channel)) != 0) {
					const float value = std::isfinite(component->timeScale) ? (std::max)(component->timeScale, 0.0f) : 1.0f;
					scale = (std::min)(scale * value, static_cast<double>((std::numeric_limits<float>::max)()));
				}
				if (!component->inheritParentScale) break;
			}
		}
		const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
		if (!hierarchy) break;
		current = hierarchy->parent;
		first = false;
	}
	const double delta = scale * sanitizeDelta(scaledDelta);
	return {static_cast<float>((std::min)(delta, static_cast<double>((std::numeric_limits<float>::max)()))),
		static_cast<float>(scale), true};
}

void Engine::from_json(const nlohmann::json& in, TimeSourceSettings& settings) {

	settings.runtimeSourceID = {};
	settings.mode = EnumAdapter<TimeSourceMode>::FromString(in.value("mode", "Hierarchy")).value_or(TimeSourceMode::Hierarchy);
	if (in.contains("source")) settings.source = in.at("source").get<SceneEntityReference>();
}

void Engine::to_json(nlohmann::json& out, const TimeSourceSettings& settings) {

	out = {{"mode", EnumAdapter<TimeSourceMode>::ToString(settings.mode)}, {"source", settings.source}};
}
