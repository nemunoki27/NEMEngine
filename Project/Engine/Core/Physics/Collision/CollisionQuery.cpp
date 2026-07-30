#include "CollisionQuery.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	CollisionQuery internal
//============================================================================
namespace {

	// FillMeshのレイヤーはCollisionComponentのtypeMask、無ければデフォルトビットを使う
	uint32_t ResolveFillMeshTypeMask(Engine::ECSWorld& world, const Engine::Entity& entity) {

		const Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
		return collision ? collision->typeMask : 1u;
	}
}

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
	if (HasRaycastTarget(targets, RaycastTargets::FillMeshes)) {
		RaycastFillMeshes(world, normalizedRay, maxDistance, layerMask, outHits);
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

void Engine::CollisionQuery::RaycastFillMeshes(ECSWorld& world, const Ray& ray, float maxDistance,
	uint32_t layerMask, std::vector<RaycastHit3D>& outHits) {

	world.ForEach<FillMeshRendererComponent>([&](const Entity& entity,
		[[maybe_unused]] FillMeshRendererComponent& fillMesh) {

		const std::span<const FillMeshPosition> positions =
			GetFillMeshPositions(world, entity);
		const std::span<const FillMeshTriangleIndex> indices =
			GetFillMeshTriangleIndices(world, entity);
		if (positions.empty() || indices.size() < 3) {
			return;
		}
		if ((ResolveFillMeshTypeMask(world, entity) & layerMask) == 0) {
			return;
		}
		const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) {
			return;
		}

		// Entityごとに最近三角形のヒットだけを採用する
		bool found = false;
		RaycastHit3D nearest{};
		const size_t vertexCount = positions.size();
		for (size_t index = 0; index + 2 < indices.size(); index += 3) {

			const uint32_t i0 = indices[index + 0].value;
			const uint32_t i1 = indices[index + 1].value;
			const uint32_t i2 = indices[index + 2].value;
			if (vertexCount <= i0 || vertexCount <= i1 || vertexCount <= i2) {
				continue;
			}

			// 頂点位置から行列を掛けてワールド座標に変換する
			Vector3 worldPos0 = Vector3::Transform(positions[i0].value, transform->worldMatrix);
			Vector3 worldPos1 = Vector3::Transform(positions[i1].value, transform->worldMatrix);
			Vector3 worldPos2 = Vector3::Transform(positions[i2].value, transform->worldMatrix);

			float distance = 0.0f;
			Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
			if (!CollisionRaycast::RayVsTriangle(ray, worldPos0, worldPos1, worldPos2, maxDistance, distance, normal)) {
				continue;
			}
			if (found && nearest.distance <= distance) {
				continue;
			}
			// ヒットした場合、trueを返す
			found = true;
			nearest.entity = entity;
			nearest.point = ray.origin + ray.direction * distance;
			nearest.normal = normal;
			nearest.distance = distance;
			nearest.shapeIndex = -1;
			nearest.triangleIndex = static_cast<int32_t>(index / 3);
			nearest.trigger = false;
		}
		if (found) {
			outHits.emplace_back(nearest);
		}
		});
}
