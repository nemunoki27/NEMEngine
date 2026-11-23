#include "ClothCollision.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>

//============================================================================
//	ClothCollisionQuery functions
//============================================================================
std::vector<Engine::ClothCollisionQuery::Collider> Engine::ClothCollisionQuery::Collect(ECSWorld& world) {

	std::vector<Collider> candidates;
	world.ForEach<CollisionComponent>([&](Entity target, const CollisionComponent& collision) {
		if (!collision.enabled || !collision.typeMask || !collision.shape.enabled || collision.shape.isTrigger ||
			!IsCollisionShape3D(collision.shape.type) || world.IsPendingDestroy(target) ||
			!IsEntityActiveInHierarchy(world, target)) return;
		ResolvedWorldTransform transform;
		if (TransformWorldUtility::ResolveWorldTransform(world, target, transform)) {
			candidates.push_back({CollisionShapeUtility::BuildShapeInstance(target, collision.shape, 0, transform), collision.typeMask});
		}
	});
	return candidates;
}

void Engine::ClothCollisionQuery::Select(std::span<const Collider> candidates, uint32_t mask,
	std::vector<CollisionShapeInstance>& shapes) {

	shapes.clear();
	for (const auto& collider : candidates) {
		if (collider.typeMask & mask) shapes.push_back(collider.shape);
	}
}

void Engine::ClothCollisionQuery::Project(
	std::span<const CollisionShapeInstance> colliders, Vector3& worldPosition, float thickness) {

	CollisionShapeInstance particle;
	particle.radius = (std::max)(thickness, 0.0f);
	particle.center = worldPosition;
	for (const auto& collider : colliders) {
		CollisionContact contact;
		if (TestCollision(collider, particle, contact) && 0.0f < contact.penetration) {
			particle.center += contact.normal * contact.penetration;
		}
	}
	worldPosition = particle.center;
}
