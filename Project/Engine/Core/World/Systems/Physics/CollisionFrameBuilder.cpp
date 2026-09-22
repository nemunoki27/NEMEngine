#include "CollisionFrameBuilder.h"

//============================================================================
//	include
//============================================================================
#include "CollisionBodyUtility.h"
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::CollisionBodyUtility;

void Engine::CollisionFrameBuilder::RebuildRuntimeShape(
	[[maybe_unused]] ECSWorld& world, CollisionRuntimeEntity& runtime) {

	runtime.hasShape = false;
	if (!runtime.collision || !runtime.transform) {
		return;
	}
	if (!runtime.collision->shape.enabled) {
		return;
	}
	runtime.shape = CollisionShapeUtility::BuildShapeInstance(
		runtime.entity, runtime.collision->shape, 0, *runtime.transform);
	runtime.hasShape = true;
}

void Engine::CollisionFrameBuilder::Collect(ECSWorld& world, std::vector<CollisionRuntimeEntity>& entities) {

	world.ForEach<CollisionComponent, TransformComponent>([&](
		Entity entity, CollisionComponent& collision, TransformComponent& transform) {

			if (!collision.enabled || !IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// Componentが持つ単一形状を判定用形状へ変換する
			CollisionRuntimeEntity runtime{};
			runtime.entity = entity;
			runtime.collision = &collision;
			runtime.state = world.TryGetComponent<CollisionRuntimeStateComponent>(entity);
			runtime.transform = &transform;
			runtime.dynamicBody = IsDynamicRigidbody(world, entity);
			runtime.surfaceBox = collision.enablePushback && IsBoxSurfaceBody(world, entity) &&
				(collision.shape.type == ColliderShapeType::AABB3D || collision.shape.type == ColliderShapeType::OBB3D) &&
				!collision.shape.isTrigger;
			RebuildRuntimeShape(world, runtime);
			if (runtime.hasShape) {
				entities.emplace_back(std::move(runtime));
			}
		});
}
