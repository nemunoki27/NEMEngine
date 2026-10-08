#include "CollisionDetectionDetail.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Engine::CollisionDetectionDetail {

	// Circle2D同士の衝突判定
	bool TestCircleCircle(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact) {

		const Engine::Vector3 delta = b.center - a.center;
		const float distanceSq = delta.x * delta.x + delta.y * delta.y;
		const float radius = a.radius + b.radius;
		if (distanceSq > radius * radius) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		const Engine::Vector3 normal = distance > kEpsilon ? Engine::Vector3(delta.x / distance, delta.y / distance, 0.0f)
														   : Engine::Vector3(1.0f, 0.0f, 0.0f);
		FillContact(a, b, outContact, normal, radius - distance, a.center + normal * a.radius);
		return true;
	}

	// Quad2Dを指定軸へ射影する
	void ProjectQuad2D(
		const Engine::CollisionShapeInstance& shape, const Engine::Vector3& axis, float& minValue, float& maxValue) {

		const float center = Engine::Vector3::Dot(shape.center, axis);
		const float radius = std::fabs(Engine::Vector3::Dot(shape.axes[0], axis)) * shape.halfExtents.x +
							 std::fabs(Engine::Vector3::Dot(shape.axes[1], axis)) * shape.halfExtents.y;
		minValue = center - radius;
		maxValue = center + radius;
	}

	// Quad2D同士の衝突判定
	bool TestQuadQuad2D(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact) {

		std::array<Engine::Vector3, 4> axes = {a.axes[0], a.axes[1], b.axes[0], b.axes[1]};
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
	bool TestCircleQuad2D(const Engine::CollisionShapeInstance& circle, const Engine::CollisionShapeInstance& quad,
		bool circleIsA, Engine::CollisionContact& outContact) {

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
}
