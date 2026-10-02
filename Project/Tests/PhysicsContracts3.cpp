#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Physics/Collision/CollisionRaycast.h>
#include <Engine/Core/Physics/Collision/CollisionQuery.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Systems/Physics/CollisionImpulse.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/PhysicsJointComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

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

	bool TestCapsuleCollisions() {

		auto makeCapsule = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, const Engine::Vector3& start,
			const Engine::Vector3& end, float radius) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.segmentStart = start;
			result.segmentEnd = end;
			result.radius = radius;
			return result;
		};
		auto makeRoundShape = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, float radius) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.radius = radius;
			return result;
		};
		auto makeBox = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, const Engine::Vector3& halfExtents) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.halfExtents = halfExtents;
			return result;
		};
		auto collidesBothWays = [](const Engine::CollisionShapeInstance& a,
			const Engine::CollisionShapeInstance& b) {

			Engine::CollisionContact contactAB{};
			Engine::CollisionContact contactBA{};
			return Engine::TestCollision(a, b, contactAB) &&
				Engine::TestCollision(b, a, contactBA) &&
				0.0f <= contactAB.penetration && 0.0f <= contactBA.penetration &&
				Engine::Vector3::Dot(contactAB.normal, contactBA.normal) < -0.99f;
		};

		const Engine::CollisionShapeInstance capsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(0.0f, -1.0f, 0.0f),
			Engine::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
		const Engine::CollisionShapeInstance circle = makeRoundShape(
			Engine::ColliderShapeType::Circle2D,
			Engine::Vector3(0.0f, 2.5f, 0.0f), 0.6f);
		const Engine::CollisionShapeInstance quad = makeBox(
			Engine::ColliderShapeType::Quad2D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(0.6f, 0.6f, 0.0f));
		const Engine::CollisionShapeInstance otherCapsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(1.5f, -1.0f, 0.0f),
			Engine::Vector3(1.5f, 1.0f, 0.0f), 0.6f);
		if (!collidesBothWays(capsule2D, circle) ||
			!collidesBothWays(capsule2D, quad) ||
			!collidesBothWays(capsule2D, otherCapsule2D)) {
			return false;
		}
		const Engine::CollisionShapeInstance crossingCapsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(-2.0f, 0.0f, 0.0f),
			Engine::Vector3(2.0f, 0.0f, 0.0f), 0.5f);
		const Engine::CollisionShapeInstance centeredQuad = makeBox(
			Engine::ColliderShapeType::Quad2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(1.0f, 1.0f, 0.0f));
		Engine::CollisionContact crossingContact2D{};
		if (!Engine::TestCollision(
			crossingCapsule2D, centeredQuad, crossingContact2D) ||
			crossingContact2D.penetration < 1.49f) {
			return false;
		}

		Engine::CollisionShapeInstance distantCircle = circle;
		distantCircle.center = Engine::Vector3(5.0f, 0.0f, 0.0f);
		Engine::CollisionContact contact{};
		if (Engine::TestCollision(capsule2D, distantCircle, contact)) {
			return false;
		}

		const Engine::CollisionShapeInstance capsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(0.0f, -1.0f, 0.0f),
			Engine::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
		const Engine::CollisionShapeInstance sphere = makeRoundShape(
			Engine::ColliderShapeType::Sphere3D,
			Engine::Vector3(0.0f, 2.5f, 0.0f), 0.6f);
		const Engine::CollisionShapeInstance aabb = makeBox(
			Engine::ColliderShapeType::AABB3D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3::AnyInit(0.6f));
		Engine::CollisionShapeInstance obb = aabb;
		obb.type = Engine::ColliderShapeType::OBB3D;
		const Engine::CollisionShapeInstance otherCapsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(1.5f, -1.0f, 0.0f),
			Engine::Vector3(1.5f, 1.0f, 0.0f), 0.6f);
		if (!collidesBothWays(capsule3D, sphere) ||
			!collidesBothWays(capsule3D, aabb) ||
			!collidesBothWays(capsule3D, obb) ||
			!collidesBothWays(capsule3D, otherCapsule3D)) {
			return false;
		}
		const Engine::CollisionShapeInstance crossingCapsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(-2.0f, 0.0f, 0.0f),
			Engine::Vector3(2.0f, 0.0f, 0.0f), 0.5f);
		const Engine::CollisionShapeInstance centeredBox = makeBox(
			Engine::ColliderShapeType::AABB3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3::AnyInit(1.0f));
		Engine::CollisionContact crossingContact3D{};
		if (!Engine::TestCollision(
			crossingCapsule3D, centeredBox, crossingContact3D) ||
			crossingContact3D.penetration < 1.49f) {
			return false;
		}

		Engine::Ray ray{};
		ray.origin = Engine::Vector3(-3.0f, 0.0f, 0.0f);
		ray.direction = Engine::Vector3(1.0f, 0.0f, 0.0f);
		float distance = 0.0f;
		Engine::Vector3 normal{};
		if (!Engine::CollisionRaycast::RayVsCapsule(
			ray, capsule3D, 10.0f, distance, normal) ||
			std::abs(distance - 2.0f) > 0.0001f || normal.x > -0.99f) {
			return false;
		}

		Engine::Ray capRay{};
		capRay.origin = Engine::Vector3(0.0f, 3.0f, 0.0f);
		capRay.direction = Engine::Vector3(0.4f, -1.0f, 0.0f).Normalize();
		float capDistance = 0.0f;
		Engine::Vector3 capNormal{};
		float capsuleDistance = 0.0f;
		Engine::Vector3 capsuleNormal{};
		if (!Engine::CollisionRaycast::RayVsSphere(
			capRay, capsule3D.segmentEnd, capsule3D.radius,
			10.0f, capDistance, capNormal) ||
			!Engine::CollisionRaycast::RayVsCapsule(
				capRay, capsule3D, 10.0f, capsuleDistance, capsuleNormal) ||
			std::abs(capsuleDistance - capDistance) > 0.0001f) {
			return false;
		}

		Engine::TransformComponent transform{};
		transform.worldMatrix = Engine::Matrix4x4::Identity();
		Engine::CollisionShape authored2D{};
		authored2D.type = Engine::ColliderShapeType::Capsule2D;
		authored2D.capsuleSize2D = Engine::Vector2(2.0f, 4.0f);
		const Engine::CollisionShapeInstance built2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), authored2D, 0, transform);
		Engine::CollisionShape inverted2D = authored2D;
		inverted2D.capsuleSize2D = Engine::Vector2(4.0f, 2.0f);
		const Engine::CollisionShapeInstance invertedBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), inverted2D, 0, transform);
		Engine::CollisionShape horizontal2D = inverted2D;
		horizontal2D.capsuleAxis = Engine::CapsuleAxis::X;
		const Engine::CollisionShapeInstance horizontalBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), horizontal2D, 0, transform);
		Engine::CollisionShape rotated2D = authored2D;
		rotated2D.offset = Engine::Vector3(0.0f, 0.0f, 5.0f);
		rotated2D.rotationDegrees = Engine::Vector3(30.0f, 45.0f, 90.0f);
		const Engine::CollisionShapeInstance rotatedBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), rotated2D, 0, transform);
		Engine::CollisionShape authored3D{};
		authored3D.type = Engine::ColliderShapeType::Capsule3D;
		authored3D.capsuleHeight = 3.0f;
		authored3D.capsuleAxis = Engine::CapsuleAxis::Z;
		const Engine::CollisionShapeInstance built3D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), authored3D, 0, transform);
		nlohmann::json capsuleJson = authored3D;
		const Engine::CollisionShape restored3D =
			capsuleJson.get<Engine::CollisionShape>();
		return Engine::IsCollisionShape2D(built2D.type) &&
			Engine::IsCollisionShape3D(built3D.type) &&
			std::abs(built2D.radius - 1.0f) <= 0.0001f &&
			std::abs(built2D.segmentStart.y + 1.0f) <= 0.0001f &&
			std::abs(built2D.segmentEnd.y - 1.0f) <= 0.0001f &&
			std::abs(invertedBuilt2D.radius - 1.0f) <= 0.0001f &&
			std::abs(invertedBuilt2D.segmentStart.y) <= 0.0001f &&
			std::abs(invertedBuilt2D.segmentEnd.y) <= 0.0001f &&
			std::abs(horizontalBuilt2D.radius - 1.0f) <= 0.0001f &&
			std::abs(horizontalBuilt2D.segmentStart.x + 1.0f) <= 0.0001f &&
			std::abs(horizontalBuilt2D.segmentEnd.x - 1.0f) <= 0.0001f &&
			std::abs(rotatedBuilt2D.center.z) <= 0.0001f &&
			std::abs(rotatedBuilt2D.segmentStart.z) <= 0.0001f &&
			std::abs(rotatedBuilt2D.segmentEnd.z) <= 0.0001f &&
			std::abs(std::abs(rotatedBuilt2D.segmentStart.x) - 1.0f) <= 0.0001f &&
			std::abs(std::abs(rotatedBuilt2D.segmentEnd.x) - 1.0f) <= 0.0001f &&
			std::abs(built3D.segmentStart.z + 1.0f) <= 0.0001f &&
			std::abs(built3D.segmentEnd.z - 1.0f) <= 0.0001f &&
			std::abs(restored3D.capsuleHeight - authored3D.capsuleHeight) <= 0.0001f &&
			restored3D.capsuleAxis == authored3D.capsuleAxis;
	}

	bool TestPhysicsQueryTriggers() {

		using namespace Engine;
		ECSWorld world(ECSWorldKind::Runtime);
		auto createSphere = [&](float x, bool trigger) {

			const Entity entity = world.CreateEntity();
			auto& transform = world.AddComponent<TransformComponent>(entity);
			transform.localPos = Vector3(x, 0.0f, 0.0f);
			transform.worldMatrix = Matrix4x4::MakeTranslateMatrix(transform.localPos);
			auto& collision = world.AddComponent<CollisionComponent>(entity);
			collision.shape.type = ColliderShapeType::Sphere3D;
			collision.shape.radius = 0.5f;
			collision.shape.isTrigger = trigger;
			return entity;
		};

		const Entity trigger = createSphere(0.0f, true);
		const Entity solid = createSphere(3.0f, false);
		Ray ray{};
		ray.origin = Vector3(-5.0f, 0.0f, 0.0f);
		ray.direction = Vector3(1.0f, 0.0f, 0.0f);
		RaycastHit3D hit{};
		if (!CollisionQuery::Raycast(world, ray, 20.0f, 0xffffffffu,
			RaycastTargets::All, QueryTriggerInteraction::Ignore, hit) ||
			hit.entity != solid || hit.trigger) {

			return false;
		}
		if (!CollisionQuery::Raycast(world, ray, 20.0f, 0xffffffffu,
			RaycastTargets::All, QueryTriggerInteraction::Collide, hit) ||
			hit.entity != trigger || !hit.trigger) {

			return false;
		}

		CollisionSettings& settings = CollisionSettings::GetInstance();
		settings.EnsureLoaded();
		const bool previous = settings.GetQueriesHitTriggers();
		settings.SetQueriesHitTriggers(false);
		const bool ignored = CollisionQuery::Raycast(world, ray, 20.0f,
			0xffffffffu, RaycastTargets::All,
			QueryTriggerInteraction::UseGlobal, hit) && hit.entity == solid;
		settings.SetQueriesHitTriggers(true);
		const bool included = CollisionQuery::Raycast(world, ray, 20.0f,
			0xffffffffu, RaycastTargets::All,
			QueryTriggerInteraction::UseGlobal, hit) && hit.entity == trigger;
		settings.SetQueriesHitTriggers(previous);

		// 動的剛体同士は合成質量を使い、運動量を保ったまま離れる
		RigidbodyComponent lightBody{};
		RigidbodyComponent heavyBody{};
		lightBody.mass = 1.0f;
		heavyBody.mass = 3.0f;
		lightBody.restitution = 1.0f;
		heavyBody.restitution = 1.0f;
		lightBody.friction = 0.0f;
		heavyBody.friction = 0.0f;
		lightBody.linearVelocity = Vector3(4.0f, 0.0f, 0.0f);
		heavyBody.linearVelocity = Vector3::AnyInit(0.0f);
		CollisionShapeInstance impulseShape{};
		impulseShape.type = ColliderShapeType::Sphere3D;
		impulseShape.radius = 1.0f;
		CollisionImpulse::ResolveContactPair3D(lightBody, heavyBody,
			Vector3(-1.0f, 0.0f, 0.0f), Vector3::AnyInit(0.0f),
			Vector3::AnyInit(0.0f), impulseShape, impulseShape);
		const float momentum = lightBody.linearVelocity.x * lightBody.mass +
			heavyBody.linearVelocity.x * heavyBody.mass;
		const bool separated = lightBody.linearVelocity.x <= heavyBody.linearVelocity.x;

		// JointとCCD設定を保存後も維持する
		HingeJointComponent hinge{};
		hinge.connectedBodyLocalFileID = Engine::UUID{ 17 };
		hinge.axis = Vector3(0.0f, 1.0f, 0.0f);
		hinge.useLimits = true;
		hinge.minAngle = -35.0f;
		hinge.maxAngle = 70.0f;
		const HingeJointComponent restoredHinge =
			nlohmann::json(hinge).get<HingeJointComponent>();
		lightBody.collisionDetection = CollisionDetectionMode::Continuous;
		const RigidbodyComponent restoredBody =
			nlohmann::json(lightBody).get<RigidbodyComponent>();
		return ignored && included && separated &&
			std::abs(momentum - 4.0f) <= 0.0001f &&
			restoredHinge.connectedBodyLocalFileID == hinge.connectedBodyLocalFileID &&
			restoredHinge.useLimits && restoredHinge.minAngle == hinge.minAngle &&
			restoredHinge.maxAngle == hinge.maxAngle &&
			restoredBody.collisionDetection == CollisionDetectionMode::Continuous;
	}
}
