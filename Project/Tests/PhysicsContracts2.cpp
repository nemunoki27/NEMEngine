#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestRigidbody2DRestingContact() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		auto createQuad = [&](const char* name, const Engine::Vector3& position,
			const Engine::Vector2& halfSize, bool isStatic) {

			const Engine::Entity entity =
				Engine::SceneAuthoring::CreateGameObject(world, name);
			auto& transform =
				world.GetComponent<Engine::TransformComponent>(entity);
			transform.dimension = Engine::Dimension::Type2D;
			transform.localPos = position;
			Engine::MarkTransformSubtreeDirty(world, entity);

			auto& collision =
				world.AddComponent<Engine::CollisionComponent>(entity);
			collision.isStatic = isStatic;
			collision.shape.type = Engine::ColliderShapeType::Quad2D;
			collision.shape.halfSize2D = halfSize;
			return entity;
		};

		// 隣接する床Colliderへ同時接触してもPlayerの接地座標が揺れないことを確認する
		const Engine::Entity player = createQuad(
			"Player", Engine::Vector3(0.0f, 0.0f, 0.0f),
			Engine::Vector2(8.0f, 8.0f), false);
		auto& body =
			world.AddComponent<Engine::Rigidbody2DComponent>(player);
		body.restitution = 0.0f;
		createQuad("FloorLeft", Engine::Vector3(-10.0f, 30.0f, 0.0f),
			Engine::Vector2(10.0f, 10.0f), true);
		createQuad("FloorRight", Engine::Vector3(10.0f, 30.0f, 0.0f),
			Engine::Vector2(10.0f, 10.0f), true);

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Play;
		context.fixedDeltaTime = 1.0f / 60.0f;
		Engine::PhysicsSystem physicsSystem{};
		Engine::TransformSystem transformSystem{};
		Engine::CollisionSystem collisionSystem{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.FixedUpdate(world, context);

		float minSettledY = (std::numeric_limits<float>::max)();
		float maxSettledY = (std::numeric_limits<float>::lowest)();
		for (uint32_t step = 0; step < 300; ++step) {

			physicsSystem.FixedUpdate(world, context);
			transformSystem.FixedUpdate(world, context);
			collisionSystem.FixedUpdate(world, context);
			if (180 <= step) {
				const float y = world.GetComponent<
					Engine::TransformComponent>(player).localPos.y;
				minSettledY = (std::min)(minSettledY, y);
				maxSettledY = (std::max)(maxSettledY, y);
			}
		}

		const float settledY = world.GetComponent<
			Engine::TransformComponent>(player).localPos.y;
		const bool passed =
			maxSettledY - minSettledY <= 0.0001f &&
			std::abs(body.linearVelocity.y) <= 0.0001f &&
			std::abs(settledY - 12.001f) <= 0.001f;
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}

	bool TestInactivePhysicsSystems() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const Engine::Entity parent =
			Engine::SceneAuthoring::CreateGameObject(world, "Inactive3D");
		const Engine::Entity child =
			Engine::SceneAuthoring::CreateGameObject(world, "Inactive2D");

		Engine::HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, child, parent);

		auto& body3D = world.AddComponent<Engine::RigidbodyComponent>(parent);
		body3D.bodyType = Engine::RigidbodyType::Dynamic;
		body3D.accumulatedForce = Engine::Vector3(3.0f, 4.0f, 5.0f);
		auto& body2D = world.AddComponent<Engine::Rigidbody2DComponent>(child);
		body2D.bodyType = Engine::RigidbodyType::Dynamic;
		body2D.accumulatedForce = Engine::Vector2(3.0f, 4.0f);

		const Engine::Vector3 parentPosition =
			world.GetComponent<Engine::TransformComponent>(parent).localPos;
		const Engine::Vector3 childPosition =
			world.GetComponent<Engine::TransformComponent>(child).localPos;
		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, false) ||
			Engine::IsEntityActiveInHierarchy(world, parent) ||
			Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Play;
		context.fixedDeltaTime = 1.0f / 60.0f;
		Engine::PhysicsSystem physicsSystem{};
		physicsSystem.FixedUpdate(world, context);

		const auto& inactiveBody3D =
			world.GetComponent<Engine::RigidbodyComponent>(parent);
		const auto& inactiveBody2D =
			world.GetComponent<Engine::Rigidbody2DComponent>(child);
		if (world.GetComponent<Engine::TransformComponent>(parent).localPos != parentPosition ||
			world.GetComponent<Engine::TransformComponent>(child).localPos != childPosition ||
			inactiveBody3D.accumulatedForce != Engine::Vector3::AnyInit(0.0f) ||
			inactiveBody2D.accumulatedForce != Engine::Vector2::AnyInit(0.0f)) {
			return false;
		}

		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, true) ||
			!Engine::IsEntityActiveInHierarchy(world, parent) ||
			!Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}
		physicsSystem.FixedUpdate(world, context);
		return world.GetComponent<Engine::TransformComponent>(parent).localPos.y < parentPosition.y &&
			childPosition.y < world.GetComponent<Engine::TransformComponent>(child).localPos.y;
	}

	bool TestEditCollisionState() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Authoring);
		auto createCollider = [&](const char* name, const Engine::Vector3& position,
			Engine::Dimension dimension, Engine::ColliderShapeType shapeType) {

			const Engine::Entity entity =
				Engine::SceneAuthoring::CreateGameObject(world, name);
			auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
			transform.dimension = dimension;
			transform.localPos = position;
			Engine::MarkTransformSubtreeDirty(world, entity);

			auto& collision = world.AddComponent<Engine::CollisionComponent>(entity);
			collision.shape.type = shapeType;
			collision.shape.halfSize2D = Engine::Vector2(8.0f, 8.0f);
			collision.shape.radius = 8.0f;
			return entity;
		};

		const Engine::Entity quadA = createCollider(
			"QuadA", Engine::Vector3(0.0f, 0.0f, 0.0f),
			Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity quadB = createCollider(
			"QuadB", Engine::Vector3(4.0f, 0.0f, 0.0f),
			Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity sphereA = createCollider(
			"SphereA", Engine::Vector3(100.0f, 0.0f, 0.0f),
			Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);
		const Engine::Entity sphereB = createCollider(
			"SphereB", Engine::Vector3(104.0f, 0.0f, 0.0f),
			Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Edit;
		Engine::TransformSystem transformSystem{};
		Engine::CollisionSystem collisionSystem{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		if (!Engine::IsCollisionColliding(world, quadA) ||
			!Engine::IsCollisionColliding(world, quadB) ||
			!Engine::IsCollisionColliding(world, sphereA) ||
			!Engine::IsCollisionColliding(world, sphereB)) {

			return false;
		}

		auto& quadBTransform = world.GetComponent<Engine::TransformComponent>(quadB);
		quadBTransform.localPos.x = 40.0f;
		Engine::MarkTransformSubtreeDirty(world, quadB);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		const bool passed =
			!Engine::IsCollisionColliding(world, quadA) &&
			!Engine::IsCollisionColliding(world, quadB) &&
			Engine::IsCollisionColliding(world, sphereA) &&
			Engine::IsCollisionColliding(world, sphereB);
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}
}
