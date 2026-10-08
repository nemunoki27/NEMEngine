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
#include <Engine/Core/World/Systems/Physics/CollisionBodyUtility.h>
#include <Engine/Core/World/Systems/Physics/CollisionFrameBuilder.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/Physics/Collision/CollisionQuery.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

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

	bool TestCollisionWorldShapes() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const auto parent = Engine::SceneAuthoring::CreateGameObject(world, "Parent");
		const auto child = Engine::SceneAuthoring::CreateGameObject(world, "Collider");
		Engine::HierarchySystem hierarchy{};
		hierarchy.SetParent(world, child, parent);
		auto& collision = world.AddComponent<Engine::CollisionComponent>(child);
		collision.typeMask = 1;
		collision.shape.type = Engine::ColliderShapeType::OBB3D;
		collision.shape.halfExtents3D = {2.0f, 0.5f, 0.5f};
		auto& parentPose = world.GetComponent<Engine::TransformComponent>(parent);
		parentPose.localPos = {10.0f, 0.0f, 0.0f};
		parentPose.localScale = Engine::Vector3::AnyInit(2.0f);
		parentPose.localRotation = Engine::Quaternion::FromEulerDegrees({0.0f, 90.0f, 0.0f});
		auto& childPose = world.GetComponent<Engine::TransformComponent>(child);

		// FixedUpdateとQueryが未更新のWorld行列に依存しない
		std::vector<Engine::CollisionRuntimeEntity> entities;
		Engine::CollisionFrameBuilder::Collect(world, entities);
		if (entities.size() != 1 || (entities[0].shape.center - Engine::Vector3(10.0f, 0.0f, 0.0f)).Length() > 0.0001f ||
			(entities[0].shape.axes[0] - Engine::Vector3(0.0f, 0.0f, -1.0f)).Length() > 0.0001f) {
			return false;
		}
		Engine::Ray ray{};
		ray.origin = {};
		ray.direction = {1.0f, 0.0f, 0.0f};
		Engine::RaycastHit3D hit{};
		if (!Engine::CollisionQuery::Raycast(
				world, ray, 100.0f, 1, Engine::RaycastTargets::Colliders, Engine::QueryTriggerInteraction::Ignore, hit) ||
			hit.entity != child || std::abs(hit.distance - 9.0f) > 0.0001f) {
			return false;
		}

		// 親の非アクティブ化をQueryと通常の衝突更新へ反映する
		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, false) ||
			Engine::CollisionQuery::Raycast(world, ray, 100.0f, 1, Engine::RaycastTargets::Colliders,
				Engine::QueryTriggerInteraction::Ignore, hit)) {
			return false;
		}
		entities.clear();
		Engine::CollisionFrameBuilder::Collect(world, entities);
		if (!entities.empty() || !Engine::SceneObjectUtility::SetActiveSelf(world, parent, true) ||
			!Engine::CollisionQuery::Raycast(world, ray, 100.0f, 1, Engine::RaycastTargets::Colliders,
				Engine::QueryTriggerInteraction::Ignore, hit) || hit.entity != child) {
			return false;
		}

		// 形状のローカル回転をEntityのWorld回転へ重ねる
		childPose.localRotation = Engine::Quaternion::FromEulerDegrees({0.0f, 0.0f, 45.0f});
		collision.shape.rotationDegrees = {30.0f, 0.0f, 0.0f};
		Engine::ResolvedWorldTransform resolved{};
		if (!Engine::TransformWorldUtility::ResolveWorldTransform(world, child, resolved)) {
			return false;
		}
		const auto instance = Engine::CollisionShapeUtility::BuildShapeInstance(child, collision.shape, 0, resolved);
		const auto expectedRotation = parentPose.localRotation * childPose.localRotation *
									  Engine::Quaternion::FromEulerDegrees(collision.shape.rotationDegrees);
		const auto expectedAxis =
			Engine::Vector3::TransferNormal({0.0f, 1.0f, 0.0f}, Engine::Quaternion::MakeRotateMatrix(expectedRotation));
		if ((instance.axes[1] - expectedAxis).Length() > 0.0001f) {
			return false;
		}

		// 回転を継承しない形状は自身の設定だけを使う
		collision.shape.useTransformRotation = false;
		const auto fixed = Engine::CollisionShapeUtility::BuildShapeInstance(child, collision.shape, 0, resolved);
		const auto fixedAxis = Engine::Vector3::TransferNormal({0.0f, 1.0f, 0.0f},
			Engine::Quaternion::MakeRotateMatrix(Engine::Quaternion::FromEulerDegrees(collision.shape.rotationDegrees)));

		// ゼロと微小な拡縮でも衝突形状を等倍へ戻さない
		for (float scale : {0.0f, 0.00005f, -0.00005f, 0.0001f, 1.0f}) {
			Engine::ResolvedWorldTransform scaled;
			scaled.matrix = Engine::Matrix4x4::MakeAffineMatrix(
				Engine::Vector3::AnyInit(scale), Engine::Quaternion::Identity(), {});
			for (const auto type : {Engine::ColliderShapeType::Sphere3D, Engine::ColliderShapeType::Circle2D,
					 Engine::ColliderShapeType::AABB3D, Engine::ColliderShapeType::Quad2D,
					 Engine::ColliderShapeType::Capsule3D, Engine::ColliderShapeType::Capsule2D}) {

				Engine::CollisionShape shape;
				shape.type = type;
				shape.radius = 2.0f;
				shape.halfExtents3D = {2.0f, 3.0f, 4.0f};
				shape.halfSize2D = {2.0f, 3.0f};
				shape.capsuleHeight = 8.0f;
				shape.capsuleSize2D = {4.0f, 8.0f};
				shape.capsuleAxis = Engine::CapsuleAxis::Y;
				const auto built = Engine::CollisionShapeUtility::BuildShapeInstance(child, shape, 0, scaled);
				const float expectedScale = std::abs(scale);
				if (std::abs(built.radius - 2.0f * expectedScale) > 0.0000001f ||
					std::abs(built.halfExtents.x - 2.0f * expectedScale) > 0.0000001f ||
					std::abs(built.halfExtents.y - 3.0f * expectedScale) > 0.0000001f) {
					return false;
				}
			}
		}
		return (fixed.axes[1] - fixedAxis).Length() < 0.0001f;
	}

	bool TestCollisionParentCoordinates() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const auto parent = Engine::SceneAuthoring::CreateGameObject(world, "Parent");
		const auto child = Engine::SceneAuthoring::CreateGameObject(world, "Child");
		Engine::HierarchySystem hierarchy{};
		hierarchy.SetParent(world, child, parent);

		// 未更新の親行列に依存せずWorldの補正量を適用する
		auto& parentTransform = world.GetComponent<Engine::TransformComponent>(parent);
		parentTransform.localPos = {10.0f, 0.0f, 0.0f};
		parentTransform.localScale = Engine::Vector3::AnyInit(2.0f);
		parentTransform.localRotation = Engine::Quaternion::FromEulerDegrees({0.0f, 90.0f, 0.0f});
		auto& transform = world.GetComponent<Engine::TransformComponent>(child);
		transform.localPos = {};
		Engine::CollisionBodyUtility::MoveEntity(world, child, {2.0f, 0.0f, 0.0f});
		const auto position = Engine::Vector3::Transform({0.0f, 0.0f, 0.0f}, transform.worldMatrix);
		if ((position - Engine::Vector3(12.0f, 0.0f, 0.0f)).Length() > 0.0001f) {
			return false;
		}

		// 親の回転と拡縮を無視する場合は補正量をそのまま使う
		transform.localPos = {};
		transform.ignoreParentScale = true;
		transform.ignoreParentRotation = true;
		Engine::CollisionBodyUtility::MoveEntity(world, child, {2.0f, 0.0f, 0.0f});
		if ((transform.localPos - Engine::Vector3(2.0f, 0.0f, 0.0f)).Length() > 0.0001f) {
			return false;
		}

		// 逆行列を作れない親では変更前の座標を維持する
		transform.ignoreParentScale = false;
		transform.ignoreParentRotation = false;
		parentTransform.localScale = {};
		const auto previousPosition = transform.localPos;
		const auto previousMatrix = transform.worldMatrix;
		Engine::CollisionBodyUtility::MoveEntity(world, child, {2.0f, 0.0f, 0.0f});
		if (transform.localPos != previousPosition || transform.worldMatrix != previousMatrix) {
			return false;
		}

		// 速度と角速度も親の回転や拡縮に左右されない
		auto checkIntegration = [](bool is2D) {
			Engine::ECSWorld runtime(Engine::ECSWorldKind::Runtime);
			const auto parentEntity = Engine::SceneAuthoring::CreateGameObject(runtime, "Parent");
			const auto bodyEntity = Engine::SceneAuthoring::CreateGameObject(runtime, "Body");
			Engine::HierarchySystem hierarchySystem{};
			hierarchySystem.SetParent(runtime, bodyEntity, parentEntity);
			if (is2D) {
				auto& body = runtime.AddComponent<Engine::Rigidbody2DComponent>(bodyEntity);
				body.useGravity = false;
				body.linearDamping = 0.0f;
				body.angularDamping = 0.0f;
				body.linearVelocity = {2.0f, 0.0f};
				body.angularVelocity = 1.0f;
			} else {
				auto& body = runtime.AddComponent<Engine::RigidbodyComponent>(bodyEntity);
				body.useGravity = false;
				body.linearDamping = 0.0f;
				body.angularDamping = 0.0f;
				body.linearVelocity = {2.0f, 0.0f, 0.0f};
				body.angularVelocity = {1.0f, 0.0f, 0.0f};
			}
			auto& parentPose = runtime.GetComponent<Engine::TransformComponent>(parentEntity);
			parentPose.localPos = {10.0f, 0.0f, 0.0f};
			parentPose.localScale = Engine::Vector3::AnyInit(2.0f);
			parentPose.localRotation = Engine::Quaternion::FromEulerDegrees(
				is2D ? Engine::Vector3(0.0f, 0.0f, 90.0f) : Engine::Vector3(0.0f, 90.0f, 0.0f));
			auto& bodyPose = runtime.GetComponent<Engine::TransformComponent>(bodyEntity);
			bodyPose.dimension = is2D ? Engine::Dimension::Type2D : Engine::Dimension::Type3D;
			bodyPose.localPos = {};
			bodyPose.localRotation = Engine::Quaternion::Identity();

			Engine::SystemContext context{};
			context.mode = Engine::WorldMode::Play;
			context.fixedDeltaTime = 1.0f;
			Engine::PhysicsSystem physics{};
			physics.FixedUpdate(runtime, context);
			Engine::ResolvedWorldTransform result{};
			if (!Engine::TransformWorldUtility::ResolveWorldTransform(runtime, bodyEntity, result) ||
				(result.matrix.GetTranslationValue() - Engine::Vector3(12.0f, 0.0f, 0.0f)).Length() > 0.0001f) {
				return false;
			}
			const auto spin = Engine::Quaternion::MakeAxisAngle(
				is2D ? Engine::Vector3(0.0f, 0.0f, 1.0f) : Engine::Vector3(1.0f, 0.0f, 0.0f), 1.0f);
			const auto expectedRotation = Engine::Quaternion::Normalize(spin * parentPose.localRotation);
			if (std::abs(std::abs(Engine::Quaternion::Dot(result.rotation, expectedRotation)) - 1.0f) >= 0.0001f) {
				return false;
			}

			// 親が退化しても蓄積力を次のステップへ持ち越さない
			parentPose.localScale = {};
			const auto before = bodyPose;
			if (is2D) {
				runtime.GetComponent<Engine::Rigidbody2DComponent>(bodyEntity).accumulatedForce = {3.0f, 4.0f};
			} else {
				runtime.GetComponent<Engine::RigidbodyComponent>(bodyEntity).accumulatedForce = {3.0f, 4.0f, 5.0f};
			}
			physics.FixedUpdate(runtime, context);
			const bool consumed =
				is2D ? runtime.GetComponent<Engine::Rigidbody2DComponent>(bodyEntity).accumulatedForce == Engine::Vector2{}
					 : runtime.GetComponent<Engine::RigidbodyComponent>(bodyEntity).accumulatedForce == Engine::Vector3{};
			return consumed && bodyPose.localPos == before.localPos && bodyPose.localRotation == before.localRotation &&
				   bodyPose.worldMatrix == before.worldMatrix;
		};
		return checkIntegration(false) && checkIntegration(true);
	}

	bool TestRigidbody2DRestingContact() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		auto createQuad = [&](const char* name, const Engine::Vector3& position, const Engine::Vector2& halfSize,
							  bool isStatic) {
			const Engine::Entity entity = Engine::SceneAuthoring::CreateGameObject(world, name);
			auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
			transform.dimension = Engine::Dimension::Type2D;
			transform.localPos = position;
			Engine::MarkTransformSubtreeDirty(world, entity);

			auto& collision = world.AddComponent<Engine::CollisionComponent>(entity);
			collision.isStatic = isStatic;
			collision.shape.type = Engine::ColliderShapeType::Quad2D;
			collision.shape.halfSize2D = halfSize;
			return entity;
		};

		// 隣接する床Colliderへ同時接触してもPlayerの接地座標が揺れないことを確認する
		const Engine::Entity player =
			createQuad("Player", Engine::Vector3(0.0f, 0.0f, 0.0f), Engine::Vector2(8.0f, 8.0f), false);
		auto& body = world.AddComponent<Engine::Rigidbody2DComponent>(player);
		body.restitution = 0.0f;
		createQuad("FloorLeft", Engine::Vector3(-10.0f, 30.0f, 0.0f), Engine::Vector2(10.0f, 10.0f), true);
		createQuad("FloorRight", Engine::Vector3(10.0f, 30.0f, 0.0f), Engine::Vector2(10.0f, 10.0f), true);

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
				const float y = world.GetComponent<Engine::TransformComponent>(player).localPos.y;
				minSettledY = (std::min)(minSettledY, y);
				maxSettledY = (std::max)(maxSettledY, y);
			}
		}

		const float settledY = world.GetComponent<Engine::TransformComponent>(player).localPos.y;
		const bool passed = maxSettledY - minSettledY <= 0.0001f && std::abs(body.linearVelocity.y) <= 0.0001f &&
							std::abs(settledY - 12.001f) <= 0.001f;
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}

	bool TestInactivePhysicsSystems() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const Engine::Entity parent = Engine::SceneAuthoring::CreateGameObject(world, "Inactive3D");
		const Engine::Entity child = Engine::SceneAuthoring::CreateGameObject(world, "Inactive2D");

		Engine::HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, child, parent);

		auto& body3D = world.AddComponent<Engine::RigidbodyComponent>(parent);
		body3D.bodyType = Engine::RigidbodyType::Dynamic;
		body3D.accumulatedForce = Engine::Vector3(3.0f, 4.0f, 5.0f);
		auto& body2D = world.AddComponent<Engine::Rigidbody2DComponent>(child);
		body2D.bodyType = Engine::RigidbodyType::Dynamic;
		body2D.accumulatedForce = Engine::Vector2(3.0f, 4.0f);

		const Engine::Vector3 parentPosition = world.GetComponent<Engine::TransformComponent>(parent).localPos;
		const Engine::Vector3 childPosition = world.GetComponent<Engine::TransformComponent>(child).localPos;
		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, false) ||
			Engine::IsEntityActiveInHierarchy(world, parent) || Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Play;
		context.fixedDeltaTime = 1.0f / 60.0f;
		Engine::PhysicsSystem physicsSystem{};
		physicsSystem.FixedUpdate(world, context);

		const auto& inactiveBody3D = world.GetComponent<Engine::RigidbodyComponent>(parent);
		const auto& inactiveBody2D = world.GetComponent<Engine::Rigidbody2DComponent>(child);
		if (world.GetComponent<Engine::TransformComponent>(parent).localPos != parentPosition ||
			world.GetComponent<Engine::TransformComponent>(child).localPos != childPosition ||
			inactiveBody3D.accumulatedForce != Engine::Vector3::AnyInit(0.0f) ||
			inactiveBody2D.accumulatedForce != Engine::Vector2::AnyInit(0.0f)) {
			return false;
		}

		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, true) ||
			!Engine::IsEntityActiveInHierarchy(world, parent) || !Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}
		physicsSystem.FixedUpdate(world, context);
		return world.GetComponent<Engine::TransformComponent>(parent).localPos.y < parentPosition.y &&
			   childPosition.y < world.GetComponent<Engine::TransformComponent>(child).localPos.y;
	}

	bool TestEditCollisionState() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Authoring);
		auto createCollider = [&](const char* name, const Engine::Vector3& position, Engine::Dimension dimension,
								  Engine::ColliderShapeType shapeType) {
			const Engine::Entity entity = Engine::SceneAuthoring::CreateGameObject(world, name);
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
			"QuadA", Engine::Vector3(0.0f, 0.0f, 0.0f), Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity quadB = createCollider(
			"QuadB", Engine::Vector3(4.0f, 0.0f, 0.0f), Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity sphereA = createCollider(
			"SphereA", Engine::Vector3(100.0f, 0.0f, 0.0f), Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);
		const Engine::Entity sphereB = createCollider(
			"SphereB", Engine::Vector3(104.0f, 0.0f, 0.0f), Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Edit;
		Engine::TransformSystem transformSystem{};
		Engine::CollisionSystem collisionSystem{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		if (!Engine::IsCollisionColliding(world, quadA) || !Engine::IsCollisionColliding(world, quadB) ||
			!Engine::IsCollisionColliding(world, sphereA) || !Engine::IsCollisionColliding(world, sphereB)) {

			return false;
		}

		auto& quadBTransform = world.GetComponent<Engine::TransformComponent>(quadB);
		quadBTransform.localPos.x = 40.0f;
		Engine::MarkTransformSubtreeDirty(world, quadB);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		const bool passed = !Engine::IsCollisionColliding(world, quadA) && !Engine::IsCollisionColliding(world, quadB) &&
							Engine::IsCollisionColliding(world, sphereA) && Engine::IsCollisionColliding(world, sphereB);
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}
}
