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
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

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
}
