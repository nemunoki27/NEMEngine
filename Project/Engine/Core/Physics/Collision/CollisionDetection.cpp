#include "CollisionDetection.h"

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

	constexpr float kEpsilon = 0.0001f;

	//============================================================================
	//	utility
	//============================================================================
	// Dot結果の絶対値を取得する
	float AbsDot(const Engine::Vector3& a, const Engine::Vector3& b) {

		return std::fabs(Engine::Vector3::Dot(a, b));
	}

	// aからbへ向かう方向を取得する
	Engine::Vector3 DirectionAToB(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b) {

		return Engine::Vector3::NormalizeOr(b.center - a.center, Engine::Vector3(1.0f, 0.0f, 0.0f));
	}

	// Box上でpointに最も近い点を求める、Quadもz半幅0のBoxとして扱える
	Engine::Vector3 ClosestPointOnBox(const Engine::CollisionShapeInstance& box, const Engine::Vector3& point) {

		Engine::Vector3 closest = box.center;
		const Engine::Vector3 local = point - box.center;
		for (uint32_t i = 0; i < 3; ++i) {

			const float halfExtent = i == 0 ? box.halfExtents.x : (i == 1 ? box.halfExtents.y : box.halfExtents.z);
			const float distance = std::clamp(Engine::Vector3::Dot(local, box.axes[i]), -halfExtent, halfExtent);
			closest += box.axes[i] * distance;
		}
		return closest;
	}

	// 接触情報をCollisionContactへ詰める、pointは実際の接触面の代表点
	void FillContact(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact, const Engine::Vector3& normal, float penetration, const Engine::Vector3& point) {

		outContact.self = a.entity;
		outContact.other = b.entity;
		outContact.selfShapeIndex = a.shapeIndex;
		outContact.otherShapeIndex = b.shapeIndex;
		outContact.normal = normal;
		outContact.point = point;
		outContact.penetration = penetration;
		outContact.trigger = a.trigger || b.trigger;
	}

	//============================================================================
	//	2D判定
	//============================================================================
	// Circle2D同士の衝突判定
	bool TestCircleCircle(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact) {

		const Engine::Vector3 delta = b.center - a.center;
		const float distanceSq = delta.x * delta.x + delta.y * delta.y;
		const float radius = a.radius + b.radius;
		if (distanceSq > radius * radius) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		const Engine::Vector3 normal = distance > kEpsilon ?
			Engine::Vector3(delta.x / distance, delta.y / distance, 0.0f) :
			Engine::Vector3(1.0f, 0.0f, 0.0f);
		FillContact(a, b, outContact, normal, radius - distance, a.center + normal * a.radius);
		return true;
	}

	// Quad2Dを指定軸へ射影する
	void ProjectQuad2D(const Engine::CollisionShapeInstance& shape,
		const Engine::Vector3& axis, float& minValue, float& maxValue) {

		const float center = Engine::Vector3::Dot(shape.center, axis);
		const float radius =
			std::fabs(Engine::Vector3::Dot(shape.axes[0], axis)) * shape.halfExtents.x +
			std::fabs(Engine::Vector3::Dot(shape.axes[1], axis)) * shape.halfExtents.y;
		minValue = center - radius;
		maxValue = center + radius;
	}

	// Quad2D同士の衝突判定
	bool TestQuadQuad2D(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact) {

		std::array<Engine::Vector3, 4> axes = { a.axes[0], a.axes[1], b.axes[0], b.axes[1] };
		float minPenetration = (std::numeric_limits<float>::max)();
		Engine::Vector3 bestAxis = Engine::Vector3(1.0f, 0.0f, 0.0f);

		// SATで分離軸を探す
		for (Engine::Vector3 axis : axes) {
			axis.z = 0.0f;
			axis = Engine::Vector3::NormalizeOr(axis, Engine::Vector3(1.0f, 0.0f, 0.0f));

			float minA = 0.0f;
			float maxA = 0.0f;
			float minB = 0.0f;
			float maxB = 0.0f;
			ProjectQuad2D(a, axis, minA, maxA);
			ProjectQuad2D(b, axis, minB, maxB);

			const float penetration = std::min(maxA, maxB) - std::max(minA, minB);
			if (penetration <= 0.0f) {
				return false;
			}
			if (penetration < minPenetration) {
				minPenetration = penetration;
				bestAxis = axis;
			}
		}

		if (Engine::Vector3::Dot(bestAxis, b.center - a.center) < 0.0f) {
			bestAxis = -bestAxis;
		}
		FillContact(a, b, outContact, bestAxis, minPenetration, ClosestPointOnBox(b, a.center));
		return true;
	}

	// Circle2DとQuad2Dの衝突判定
	bool TestCircleQuad2D(const Engine::CollisionShapeInstance& circle,
		const Engine::CollisionShapeInstance& quad, bool circleIsA, Engine::CollisionContact& outContact) {

		const Engine::Vector3 local = circle.center - quad.center;
		const float localX = std::clamp(Engine::Vector3::Dot(local, quad.axes[0]), -quad.halfExtents.x, quad.halfExtents.x);
		const float localY = std::clamp(Engine::Vector3::Dot(local, quad.axes[1]), -quad.halfExtents.y, quad.halfExtents.y);
		const Engine::Vector3 closest = quad.center + quad.axes[0] * localX + quad.axes[1] * localY;
		Engine::Vector3 delta = circle.center - closest;
		delta.z = 0.0f;

		const float distance = delta.Length();
		if (distance > circle.radius) {
			return false;
		}

		Engine::Vector3 normalCircleToQuad = -Engine::Vector3::NormalizeOr(delta, DirectionAToB(circle, quad));
		float penetration = circle.radius - distance;
		if (distance <= kEpsilon) {

			// Circle中心がQuad内側にある場合は、最も近い面から法線を作る
			const float remainX = quad.halfExtents.x - std::fabs(Engine::Vector3::Dot(local, quad.axes[0]));
			const float remainY = quad.halfExtents.y - std::fabs(Engine::Vector3::Dot(local, quad.axes[1]));
			if (remainX < remainY) {
				normalCircleToQuad = quad.axes[0] * (Engine::Vector3::Dot(local, quad.axes[0]) < 0.0f ? 1.0f : -1.0f);
				penetration = circle.radius + remainX;
			} else {
				normalCircleToQuad = quad.axes[1] * (Engine::Vector3::Dot(local, quad.axes[1]) < 0.0f ? 1.0f : -1.0f);
				penetration = circle.radius + remainY;
			}
		}

		if (circleIsA) {
			FillContact(circle, quad, outContact, normalCircleToQuad, penetration, closest);
		} else {
			FillContact(quad, circle, outContact, -normalCircleToQuad, penetration, closest);
		}
		return true;
	}

	//============================================================================
	//	3D判定
	//============================================================================
	// Sphere3D同士の衝突判定
	bool TestSphereSphere(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact) {

		const Engine::Vector3 delta = b.center - a.center;
		const float distanceSq = Engine::Vector3::Dot(delta, delta);
		const float radius = a.radius + b.radius;
		if (distanceSq > radius * radius) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		const Engine::Vector3 normal = Engine::Vector3::NormalizeOr(delta, Engine::Vector3(1.0f, 0.0f, 0.0f));
		FillContact(a, b, outContact, normal, radius - distance, a.center + normal * a.radius);
		return true;
	}

	// Sphere3DとBoxの衝突判定
	bool TestSphereBox(const Engine::CollisionShapeInstance& sphere,
		const Engine::CollisionShapeInstance& box, bool sphereIsA, Engine::CollisionContact& outContact) {

		const Engine::Vector3 closest = ClosestPointOnBox(box, sphere.center);
		const Engine::Vector3 delta = sphere.center - closest;
		const float distance = delta.Length();
		if (distance > sphere.radius) {
			return false;
		}

		Engine::Vector3 normalSphereToBox = -Engine::Vector3::NormalizeOr(delta, DirectionAToB(sphere, box));
		float penetration = sphere.radius - distance;
		if (distance <= kEpsilon) {

			// Sphere中心がBox内側にある場合は、最も近い面から法線を作る
			const Engine::Vector3 local = sphere.center - box.center;
			float minRemain = (std::numeric_limits<float>::max)();
			for (uint32_t i = 0; i < 3; ++i) {
				const float halfExtent = i == 0 ? box.halfExtents.x : (i == 1 ? box.halfExtents.y : box.halfExtents.z);
				const float distanceOnAxis = Engine::Vector3::Dot(local, box.axes[i]);
				const float remain = halfExtent - std::fabs(distanceOnAxis);
				if (remain < minRemain) {
					minRemain = remain;
					normalSphereToBox = box.axes[i] * (distanceOnAxis < 0.0f ? 1.0f : -1.0f);
				}
			}
			penetration = sphere.radius + minRemain;
		}

		if (sphereIsA) {
			FillContact(sphere, box, outContact, normalSphereToBox, penetration, closest);
		} else {
			FillContact(box, sphere, outContact, -normalSphereToBox, penetration, closest);
		}
		return true;
	}

	// Boxを指定軸へ射影した半径を求める
	float ProjectBoxRadius(const Engine::CollisionShapeInstance& box, const Engine::Vector3& axis) {

		return AbsDot(box.axes[0], axis) * box.halfExtents.x +
			AbsDot(box.axes[1], axis) * box.halfExtents.y +
			AbsDot(box.axes[2], axis) * box.halfExtents.z;
	}

	// AABB3D / OBB3D同士の衝突判定
	bool TestBoxBox3D(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact) {

		std::array<Engine::Vector3, 15> axes{};
		uint32_t axisCount = 0;
		for (uint32_t i = 0; i < 3; ++i) {
			axes[axisCount++] = a.axes[i];
			axes[axisCount++] = b.axes[i];
		}
		for (uint32_t i = 0; i < 3; ++i) {
			for (uint32_t j = 0; j < 3; ++j) {
				const Engine::Vector3 axis = Engine::Vector3::Cross(a.axes[i], b.axes[j]);
				if (axis.Length() > kEpsilon) {
					axes[axisCount++] = axis.Normalize();
				}
			}
		}

		float minPenetration = (std::numeric_limits<float>::max)();
		Engine::Vector3 bestAxis = Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 centerDelta = b.center - a.center;

		// SATで分離軸を探す
		for (uint32_t i = 0; i < axisCount; ++i) {

			const Engine::Vector3 axis = Engine::Vector3::NormalizeOr(axes[i], Engine::Vector3(1.0f, 0.0f, 0.0f));
			const float distance = std::fabs(Engine::Vector3::Dot(centerDelta, axis));
			const float radius = ProjectBoxRadius(a, axis) + ProjectBoxRadius(b, axis);
			const float penetration = radius - distance;
			if (penetration <= 0.0f) {
				return false;
			}
			if (penetration < minPenetration) {
				minPenetration = penetration;
				bestAxis = axis;
			}
		}

		if (Engine::Vector3::Dot(bestAxis, centerDelta) < 0.0f) {
			bestAxis = -bestAxis;
		}
		FillContact(a, b, outContact, bestAxis, minPenetration, ClosestPointOnBox(b, a.center));
		return true;
	}
}

//============================================================================
//	CollisionDetection functions
//============================================================================
bool Engine::IsCollisionShape2D(ColliderShapeType type) {

	return type == ColliderShapeType::Circle2D || type == ColliderShapeType::Quad2D;
}

bool Engine::IsCollisionShape3D(ColliderShapeType type) {

	return type == ColliderShapeType::Sphere3D ||
		type == ColliderShapeType::AABB3D ||
		type == ColliderShapeType::OBB3D;
}

bool Engine::TestCollision(const CollisionShapeInstance& a, const CollisionShapeInstance& b, CollisionContact& outContact) {

	// 2Dと3Dは別空間として扱い、混在判定は行わない
	if (IsCollisionShape2D(a.type) != IsCollisionShape2D(b.type)) {
		return false;
	}

	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Circle2D) {
		return TestCircleCircle(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Quad2D) {
		return TestCircleQuad2D(a, b, true, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Circle2D) {
		return TestCircleQuad2D(b, a, false, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Quad2D) {
		return TestQuadQuad2D(a, b, outContact);
	}

	if (a.type == ColliderShapeType::Sphere3D && b.type == ColliderShapeType::Sphere3D) {
		return TestSphereSphere(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Sphere3D && (b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestSphereBox(a, b, true, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) && b.type == ColliderShapeType::Sphere3D) {
		return TestSphereBox(b, a, false, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) &&
		(b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestBoxBox3D(a, b, outContact);
	}
	return false;
}
