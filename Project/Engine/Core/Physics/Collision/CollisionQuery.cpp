#include "CollisionQuery.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CollisionQuery internal
//============================================================================
namespace {

	using namespace Engine;

	template<class OnHit>
	void VisitHits(ECSWorld& world, const Ray& ray, float maxDistance, bool is2D, uint32_t layerMask,
		QueryTriggerInteraction triggers, std::span<const Entity> excluded, OnHit&& onHit) {

		if (layerMask == 0 || maxDistance <= 0.0f) {
			return;
		}
		bool includeTriggers = triggers == QueryTriggerInteraction::Collide;
		if (triggers == QueryTriggerInteraction::UseGlobal) {
			auto& settings = CollisionSettings::GetInstance();
			settings.EnsureLoaded();
			includeTriggers = settings.GetQueriesHitTriggers();
		}
		world.ForEach<CollisionComponent>([&](const Entity& entity, CollisionComponent& collision) {
			const CollisionShape& shape = collision.shape;
			if (!collision.enabled || !shape.enabled || (collision.typeMask & layerMask) == 0 ||
				(!includeTriggers && shape.isTrigger) || (is2D ? !IsCollisionShape2D(shape.type) : !IsCollisionShape3D(shape.type)) ||
				std::find(excluded.begin(), excluded.end(), entity) != excluded.end() ||
				world.IsPendingDestroy(entity) || !IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// LateUpdate前も現在の親追従から判定する
			ResolvedWorldTransform resolved{};
			if (!TransformWorldUtility::ResolveWorldTransform(world, entity, resolved)) {
				return;
			}
			const CollisionShapeInstance instance = CollisionShapeUtility::BuildShapeInstance(entity, shape, 0, resolved);
			RaycastHit3D hit{};
			const bool intersects = is2D ? CollisionRaycast::RayVsShape2D(ray, instance, maxDistance, hit.distance, hit.normal)
				: CollisionRaycast::RayVsShape(ray, instance, maxDistance, hit.distance, hit.normal);
			if (intersects) {
				hit.entity = entity;
				hit.trigger = shape.isTrigger;
				hit.shapeIndex = 0;
				hit.point = ray.origin + ray.direction * hit.distance;
				onHit(hit, maxDistance);
			}
		});
	}

	bool FindNearest(ECSWorld& world, const Ray& ray, float maxDistance, bool is2D, uint32_t layerMask,
		QueryTriggerInteraction triggers, std::span<const Entity> excluded, RaycastHit3D& outHit) {

		bool found = false;
		VisitHits(world, ray, maxDistance, is2D, layerMask, triggers, excluded, [&](const RaycastHit3D& hit, float& limit) {
			if (!found || hit.distance < limit) {
				found = true;
				outHit = hit;
				limit = hit.distance;
			}
		});
		return found;
	}
}

//============================================================================
//	CollisionQuery classMethods
//============================================================================
bool Engine::CollisionQuery::Raycast(ECSWorld& world, const Ray& ray, float maxDistance, uint32_t layerMask,
	RaycastTargets targets, QueryTriggerInteraction triggerInteraction, RaycastHit3D& outHit) {

	const Ray normalized{ray.origin, Vector3::NormalizeOr(ray.direction, Vector3::AnyInit(0.0f))};
	return HasRaycastTarget(targets, RaycastTargets::Colliders) && 0.0001f < normalized.direction.Length() &&
		FindNearest(world, normalized, maxDistance, false, layerMask, triggerInteraction, {}, outHit);
}

void Engine::CollisionQuery::RaycastAll(ECSWorld& world, const Ray& ray, float maxDistance, uint32_t layerMask,
	RaycastTargets targets, QueryTriggerInteraction triggerInteraction, std::vector<RaycastHit3D>& outHits) {

	outHits.clear();
	const Ray normalized{ray.origin, Vector3::NormalizeOr(ray.direction, Vector3::AnyInit(0.0f))};
	if (!HasRaycastTarget(targets, RaycastTargets::Colliders) || normalized.direction.Length() <= 0.0001f) {
		return;
	}
	VisitHits(world, normalized, maxDistance, false, layerMask, triggerInteraction, {},
		[&](const RaycastHit3D& hit, float&) { outHits.emplace_back(hit); });
	std::sort(
		outHits.begin(), outHits.end(), [](const RaycastHit3D& a, const RaycastHit3D& b) { return a.distance < b.distance; });
}

bool Engine::RaycastBeamSegment(ECSWorld& world, const Vector3& start, const Vector3& end, bool is2D,
	uint32_t layerMask, QueryTriggerInteraction triggerInteraction, std::span<const Entity> excluded, RaycastHit3D& outHit) {

	const Vector3 segment = end - start;
	Vector3 direction = segment;
	if (is2D) {
		direction.z = 0.0f;
	}
	const float length = direction.Length();
	if (!std::isfinite(length) || length <= 0.000001f) {
		return false;
	}
	const Ray ray{start, direction / length};
	if (!FindNearest(world, ray, length, is2D, layerMask, triggerInteraction, excluded, outHit)) {
		return false;
	}
	if (is2D) {
		// 判定はXYで行い、ヒット位置と距離は元の線分上で返す
		const float t = outHit.distance / length;
		outHit.point = start + segment * t;
		outHit.distance = segment.Length() * t;
	}
	return true;
}

bool Engine::HasRaycastTarget(RaycastTargets targets, RaycastTargets target) {

	return (static_cast<uint32_t>(targets) & static_cast<uint32_t>(target)) != 0;
}
