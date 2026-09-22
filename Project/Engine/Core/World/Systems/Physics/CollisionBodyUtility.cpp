#include "CollisionBodyUtility.h"

//============================================================================
//	include
//============================================================================
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

namespace Engine::CollisionBodyUtility {

	bool IsDynamicRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (world.HasComponent<Engine::RigidbodyComponent>(entity)) {
			return world.GetComponent<Engine::RigidbodyComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		if (world.HasComponent<Engine::Rigidbody2DComponent>(entity)) {
			return world.GetComponent<Engine::Rigidbody2DComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		return false;
	}

	bool HasRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Engine::RigidbodyComponent>(entity) ||
			world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}

	bool IsBoxSurfaceBody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Static;
		}
		return !world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}
}
