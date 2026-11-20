#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	BeamRendererRecord* ResolveBeam(ManagedNativeEntity native, ECSWorld*& world, Entity& owner) {

		world = ResolveWorld(native);
		owner = ResolveEntity(native);
		if (!world || !world->IsAlive(owner) || world->IsPendingDestroy(owner)) return nullptr;
		return TryGetBeamRenderer(*world, owner);
	}

	BeamEndpoint* ResolvePoint(BeamRendererSettings& settings, int32_t index) {

		if (index == -1) return &settings.start;
		if (index == -2) return &settings.end;
		if (index < 0 || settings.waypoints.size() <= static_cast<size_t>(index)) return nullptr;
		return &settings.waypoints[index];
	}
}

int32_t ManagedScriptRuntime::BeamCommandCallback(ManagedNativeEntity native, int32_t command) {

	ECSWorld* world;
	Entity owner;
	auto* record = ResolveBeam(native, world, owner);
	if (!record) return -1;
	switch (command) {
	case 0: PlayBeam(*world, owner); break;
	case 1: StopBeam(*world, owner); break;
	case 2: return record->runtime.playing ? 1 : 0;
	case 3: return static_cast<int32_t>(record->settings.waypoints.size());
	default: return -1;
	}
	return 0;
}

int32_t ManagedScriptRuntime::ReadBeamEndpointCallback(ManagedNativeEntity native, int32_t index, int32_t* mode,
	ManagedVector3* position, ManagedNativeEntity* target) {

	ECSWorld* world;
	Entity owner;
	auto* record = ResolveBeam(native, world, owner);
	if (!record || !mode || !position || !target) return 0;
	const auto* point = ResolvePoint(record->settings, index);
	if (!point) return 0;
	*mode = static_cast<int32_t>(point->mode);
	*position = ToManagedVector3(point->position);
	const Entity object = point->runtimeObject.IsValid() ? point->runtimeObject : point->object.Resolve(*world, owner);
	*target = world->IsAlive(object) && !world->IsPendingDestroy(object) ? MakeNativeEntity(*world, object) : ManagedNativeEntity{};
	return 1;
}

int32_t ManagedScriptRuntime::WriteBeamEndpointCallback(ManagedNativeEntity native, int32_t index, int32_t mode,
	ManagedVector3 position, ManagedNativeEntity target) {

	ECSWorld* world;
	Entity owner;
	auto* record = ResolveBeam(native, world, owner);
	if (!record || mode < 0 || static_cast<int32_t>(BeamEndpointMode::Object) < mode ||
		!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return 0;
	BeamEndpoint next{static_cast<BeamEndpointMode>(mode), ToVector3(position)};
	if (next.mode == BeamEndpointMode::Object && target.IsValid()) {
		const Entity object = ResolveEntity(target);
		if (ResolveWorld(target) != world || !world->IsAlive(object) || world->IsPendingDestroy(object)) return 0;
		next.object = SceneEntityReference::FromEntity(*world, object);
		next.runtimeObject = object;
	}
	if (index == -3) record->settings.waypoints.push_back(next);
	else {
		auto* point = ResolvePoint(record->settings, index);
		if (!point) return 0;
		*point = next;
	}
	world->MarkComponentModified<BeamRendererComponent>(owner);
	return 1;
}

int32_t ManagedScriptRuntime::EditBeamWaypointsCallback(ManagedNativeEntity native, int32_t operation, int32_t from, int32_t to) {

	ECSWorld* world;
	Entity owner;
	auto* record = ResolveBeam(native, world, owner);
	if (!record) return 0;
	auto& points = record->settings.waypoints;
	if (from < 0 || points.size() <= static_cast<size_t>(from)) return 0;
	if (operation == 0) points.erase(points.begin() + from);
	else if (operation == 1 && 0 <= to && static_cast<size_t>(to) < points.size()) {
		if (from < to) std::rotate(points.begin() + from, points.begin() + from + 1, points.begin() + to + 1);
		else std::rotate(points.begin() + to, points.begin() + from, points.begin() + from + 1);
	} else return 0;
	world->MarkComponentModified<BeamRendererComponent>(owner);
	return 1;
}

int32_t ManagedScriptRuntime::GetBeamHitCallback(ManagedNativeEntity native, ManagedRaycastHit* hit, int32_t* segment) {

	ECSWorld* world;
	Entity owner;
	const auto* record = ResolveBeam(native, world, owner);
	if (!record || !record->runtime.hasHit || !hit || !segment || !world->IsAlive(record->runtime.hit.entity) ||
		world->IsPendingDestroy(record->runtime.hit.entity)) return 0;
	*hit = ToManagedRaycastHit(*world, record->runtime.hit);
	*segment = record->runtime.hitSegment;
	return 1;
}
