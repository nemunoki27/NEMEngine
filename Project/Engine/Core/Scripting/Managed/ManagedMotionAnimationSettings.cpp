#include "ManagedMotionAnimationSettings.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>

// c++
#include <cmath>

using namespace Engine;

bool Engine::ReadMotionAnimationSettings(ECSWorld& world, Entity entity, nlohmann::json& data) {

	const auto* record = TryGetMotionAnimation(world, entity);
	if (!record) return false;
	data = record->settings;
	const auto& runtime = record->runtime;
	data["runtime"] = {{"time", runtime.time}, {"position", runtime.position.ToJson()}, {"rotation", runtime.rotation.ToJson()},
		{"deltaPosition", runtime.deltaPosition.ToJson()}, {"deltaRotation", runtime.deltaRotation.ToJson()},
		{"isPlaying", runtime.playing}, {"isPaused", runtime.paused}, {"isFinished", runtime.finished},
		{"cycles", runtime.cycles}, {"diagnostic", runtime.diagnostic}};
	data["events"] = {{"segments", runtime.pendingSegments}, {"cycles", runtime.pendingCycles}, {"completions", runtime.pendingCompletions}};
	return true;
}

bool Engine::ApplyMotionAnimationSettings(ECSWorld& world, Entity entity, const nlohmann::json& data) {

	auto* record = TryGetMotionAnimation(world, entity);
	if (!record) return false;
	if (data.contains("command")) {
		const int32_t request = data.at("command").get<int32_t>();
		if (request < 1 || request > 10) return false;
		if (request == 10) {
			record->runtime.pendingSegments.clear();
			record->runtime.pendingCycles = record->runtime.pendingCompletions = 0;
			return true;
		}
		const double time = data.value("time", 0.0);
		if (!std::isfinite(time) || time < 0) return false;
		record->runtime.seekTime = time;
		record->runtime.request = request;
		return true;
	}
	if (data.contains("targetNative") || data.contains("splineNative")) {
		const bool spline = data.contains("splineNative");
		const auto& value = data.at(spline ? "splineNative" : "targetNative");
		ManagedNativeEntity native{};
		native.world.index = value.at("world").at("index").get<uint32_t>();
		native.world.generation = value.at("world").at("generation").get<uint32_t>();
		native.index = value.at("index").get<uint32_t>();
		native.generation = value.at("generation").get<uint32_t>();
		Entity reference = Entity::Null();
		if (native.IsValid()) {
			if (ResolveWorld(native) != &world || !world.IsAlive(ResolveEntity(native))) return false;
			reference = ResolveEntity(native);
			if (world.IsPendingDestroy(reference)) return false;
			if (spline && !world.TryGetComponentForBinding<SplineComponent>(reference)) return false;
		}
		if (spline) {
			record->settings.spline = native.IsValid() ? SceneEntityReference::FromEntity(world, reference) : SceneEntityReference{};
			record->runtime.hasSplineOverride = native.IsValid();
			record->runtime.splineOverride = reference;
			record->runtime.splineSamples.clear();
			record->runtime.resourcesDirty = true;
		} else {
			record->settings.target = native.IsValid() ? SceneEntityReference::FromEntity(world, reference) : SceneEntityReference{};
			record->runtime.hasTargetOverride = native.IsValid();
			record->runtime.targetOverride = reference;
			if (data.value("capture", true)) record->runtime.request = 9;
		}
		world.MarkComponentModified<MotionAnimationComponent>(entity);
		return true;
	}
	nlohmann::json current = record->settings;
	current.merge_patch(data);
	auto candidate = current.get<MotionAnimationSettings>();
	if (!data.contains("timeSource")) PreserveTimeSourceRuntime(record->settings.timeSource, candidate.timeSource);
	SetMotionAnimationSettings(world, entity, candidate);
	return true;
}
