#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>

#include <cmath>

using namespace Engine;

namespace {

	bool IsFinite(const ManagedVector3& value) {

		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	TrailRendererRecord* ResolveTrail(ManagedNativeEntity native, ECSWorld*& world, Entity& entity) {

		world = ResolveWorld(native);
		entity = ResolveEntity(native);
		if (!world || !world->IsAlive(entity) || world->IsPendingDestroy(entity)) return nullptr;
		return TryGetTrailRenderer(*world, entity);
	}
}

int32_t ManagedScriptRuntime::TrailCommandCallback(ManagedNativeEntity native, int32_t command, ManagedVector3 a, ManagedVector3 b) {

	ECSWorld* world;
	Entity entity;
	auto* record = ResolveTrail(native, world, entity);
	if (!record) return -1;
	switch (command) {
	case 0: ClearTrail(*world, entity); break;
	case 1:
		if (!IsFinite(a) || record->settings.mode != TrailMode::SinglePoint) return -1;
		AddTrailPosition(*world, entity, ToVector3(a));
		break;
	case 2:
		if (!IsFinite(a) || !IsFinite(b) || record->settings.mode != TrailMode::TwoPoints) return -1;
		AddTrailSegment(*world, entity, ToVector3(a), ToVector3(b));
		break;
	case 3: SetTrailEmitting(*world, entity, true); break;
	case 4: SetTrailEmitting(*world, entity, false); break;
	case 5: return record->settings.emitting ? 1 : 0;
	case 6: return static_cast<int32_t>(record->runtime.points.size());
	default: return -1;
	}
	return 0;
}

ManagedNativeEntity ManagedScriptRuntime::GetTrailEndpointCallback(ManagedNativeEntity native, int32_t tip) {

	ECSWorld* world;
	Entity owner;
	const auto* record = ResolveTrail(native, world, owner);
	if (!record) return {};
	const auto& reference = tip ? record->settings.tip : record->settings.root;
	const Entity runtime = tip ? record->runtime.tipOverride : record->runtime.rootOverride;
	const Entity point = runtime != Entity::Null() ? runtime : reference.Resolve(*world, owner);
	return world->IsAlive(point) && !world->IsPendingDestroy(point) ? MakeNativeEntity(*world, point) : ManagedNativeEntity{};
}

int32_t ManagedScriptRuntime::SetTrailEndpointCallback(ManagedNativeEntity native, int32_t tip, ManagedNativeEntity target) {

	ECSWorld* world;
	Entity owner;
	auto* record = ResolveTrail(native, world, owner);
	if (!record) return 0;
	const Entity point = target.IsValid() ? ResolveEntity(target) : Entity::Null();
	if (target.IsValid() && (ResolveWorld(target) != world || !world->IsAlive(point) || world->IsPendingDestroy(point))) return 0;
	auto& reference = tip ? record->settings.tip : record->settings.root;
	auto& runtime = tip ? record->runtime.tipOverride : record->runtime.rootOverride;
	if (target.IsValid() ? (runtime == point || (!runtime.IsValid() && reference.Resolve(*world, owner) == point)) :
		(!runtime.IsValid() && !reference.localFileID)) return 1;
	reference = target.IsValid() ? SceneEntityReference::FromEntity(*world, point) : SceneEntityReference{};
	runtime = reference.localFileID ? Entity::Null() : point;
	BreakTrail(*world, owner);
	world->MarkComponentModified<TrailRendererComponent>(owner);
	return 1;
}
