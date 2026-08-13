#include "CollisionQuery.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	CollisionQuery classMethods
//============================================================================
bool Engine::CollisionQuery::Raycast(ECSWorld& world, const Ray& ray, float maxDistance,
	uint32_t layerMask, RaycastTargets targets, RaycastHit3D& outHit) {

	std::vector<RaycastHit3D> hits{};
	RaycastAll(world, ray, maxDistance, layerMask, targets, hits);
	if (hits.empty()) {
		return false;
	}
	outHit = hits.front();
	return true;
}

void Engine::CollisionQuery::RaycastAll(ECSWorld& world, const Ray& ray, float maxDistance,
	uint32_t layerMask, RaycastTargets targets, std::vector<RaycastHit3D>& outHits) {

	outHits.clear();

	// 方向が無効なレイは判定しない
	Ray normalizedRay = ray;
	normalizedRay.direction = Vector3::NormalizeOr(ray.direction, Vector3::AnyInit(0.0f));
	if (normalizedRay.direction.Length() <= 0.0001f || maxDistance <= 0.0f) {
		return;
	}

	if (HasRaycastTarget(targets, RaycastTargets::Colliders)) {
		RaycastColliders(world, normalizedRay, maxDistance, layerMask, outHits);
	}
	std::sort(outHits.begin(), outHits.end(),
		[](const RaycastHit3D& a, const RaycastHit3D& b) { return a.distance < b.distance; });
}

void Engine::CollisionQuery::RaycastColliders(ECSWorld& world, const Ray& ray, float maxDistance,
	uint32_t layerMask, std::vector<RaycastHit3D>& outHits) {

	world.ForEach<CollisionComponent>([&](const Entity& entity, CollisionComponent& collision) {

		if (!collision.enabled || (collision.typeMask & layerMask) == 0) {
			return;
		}
		const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) {
			return;
		}

		const std::span<const CollisionShape> shapes =
			GetCollisionShapes(world, entity);
		for (uint32_t shapeIndex = 0; shapeIndex < static_cast<uint32_t>(shapes.size()); ++shapeIndex) {

			const CollisionShape& shape = shapes[shapeIndex];
			if (!shape.enabled || !IsCollisionShape3D(shape.type)) {
				continue;
			}

			const CollisionShapeInstance instance =
				CollisionShapeUtility::BuildShapeInstance(entity, shape, shapeIndex, *transform);
			float distance = 0.0f;
			Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
			if (!CollisionRaycast::RayVsShape(ray, instance, maxDistance, distance, normal)) {
				continue;
			}

			RaycastHit3D hit{};
			hit.entity = entity;
			hit.point = ray.origin + ray.direction * distance;
			hit.normal = normal;
			hit.distance = distance;
			hit.shapeIndex = static_cast<int32_t>(shapeIndex);
			hit.trigger = shape.isTrigger;
			outHits.emplace_back(hit);
		}
		});
}
