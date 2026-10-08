#include "CollisionDetectionDetail.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine::CollisionDetectionDetail {

	// カプセルと円または球の衝突判定
	bool TestCapsuleSphere(const Engine::CollisionShapeInstance& capsule, const Engine::CollisionShapeInstance& sphere,
		bool capsuleIsA, bool is2D, Engine::CollisionContact& outContact) {

		// 中心線と相手を判定次元へ揃える
		const Engine::Vector3 segmentStart = is2D ? Flatten2D(capsule.segmentStart) : capsule.segmentStart;
		const Engine::Vector3 segmentEnd = is2D ? Flatten2D(capsule.segmentEnd) : capsule.segmentEnd;
		const Engine::Vector3 sphereCenter = is2D ? Flatten2D(sphere.center) : sphere.center;
		const Engine::Vector3 closest = ClosestPointOnSegment(segmentStart, segmentEnd, sphereCenter);
		const Engine::Vector3 delta = sphereCenter - closest;
		const float distanceSq = Engine::Vector3::Dot(delta, delta);
		const float radius = capsule.radius + sphere.radius;
		if (radius * radius < distanceSq) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		Engine::Vector3 fallback = sphereCenter - (is2D ? Flatten2D(capsule.center) : capsule.center);
		fallback = Engine::Vector3::NormalizeOr(fallback, Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 capsuleToSphere = Engine::Vector3::NormalizeOr(delta, fallback);
		const Engine::Vector3 point = closest + capsuleToSphere * capsule.radius;
		// 法線と接触対象を入力順へ揃える
		if (capsuleIsA) {
			FillContact(capsule, sphere, outContact, capsuleToSphere, radius - distance, point);
		} else {
			FillContact(sphere, capsule, outContact, -capsuleToSphere, radius - distance, point);
		}
		return true;
	}

	// カプセル同士の衝突判定
	bool TestCapsuleCapsule(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b, bool is2D,
		Engine::CollisionContact& outContact) {

		// 中心線同士の最近点を求める
		const Engine::Vector3 startA = is2D ? Flatten2D(a.segmentStart) : a.segmentStart;
		const Engine::Vector3 endA = is2D ? Flatten2D(a.segmentEnd) : a.segmentEnd;
		const Engine::Vector3 startB = is2D ? Flatten2D(b.segmentStart) : b.segmentStart;
		const Engine::Vector3 endB = is2D ? Flatten2D(b.segmentEnd) : b.segmentEnd;
		Engine::Vector3 pointA{};
		Engine::Vector3 pointB{};
		ClosestPointsOnSegments(startA, endA, startB, endB, pointA, pointB);

		const Engine::Vector3 delta = pointB - pointA;
		const float distanceSq = Engine::Vector3::Dot(delta, delta);
		const float radius = a.radius + b.radius;
		if (radius * radius < distanceSq) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		Engine::Vector3 fallback = (is2D ? Flatten2D(b.center) : b.center) - (is2D ? Flatten2D(a.center) : a.center);
		fallback = Engine::Vector3::NormalizeOr(fallback, Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 normal = Engine::Vector3::NormalizeOr(delta, fallback);
		FillContact(a, b, outContact, normal, radius - distance, pointA + normal * a.radius);
		return true;
	}

	// カプセルと回転Boxの衝突判定、dimensionCountが2ならQuadとして扱う
	bool TestCapsuleBox(const Engine::CollisionShapeInstance& capsule, const Engine::CollisionShapeInstance& box,
		bool capsuleIsA, uint32_t dimensionCount, Engine::CollisionContact& outContact) {

		const bool is2D = dimensionCount == 2;
		const Engine::Vector3 start = is2D ? Flatten2D(capsule.segmentStart) : capsule.segmentStart;
		const Engine::Vector3 end = is2D ? Flatten2D(capsule.segmentEnd) : capsule.segmentEnd;
		// 平面判定では箱の基底もXYへ揃える
		Engine::CollisionShapeInstance queryBox = box;
		if (is2D) {
			queryBox.center = Flatten2D(queryBox.center);
			queryBox.axes[0] = Engine::Vector3::NormalizeOr(Flatten2D(queryBox.axes[0]), Engine::Vector3(1.0f, 0.0f, 0.0f));
			queryBox.axes[1] = Engine::Vector3::NormalizeOr(Flatten2D(queryBox.axes[1]), Engine::Vector3(0.0f, 1.0f, 0.0f));
			queryBox.axes[2] = Engine::Vector3(0.0f, 0.0f, 1.0f);
		}

		Engine::Vector3 segmentPoint{};
		Engine::Vector3 boxPoint{};
		ClosestPointsSegmentBox(start, end, queryBox, dimensionCount, segmentPoint, boxPoint);
		const Engine::Vector3 delta = segmentPoint - boxPoint;
		const float distanceSq = Engine::Vector3::Dot(delta, delta);
		if (capsule.radius * capsule.radius < distanceSq) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		Engine::Vector3 capsuleToBox{};
		float penetration = capsule.radius - distance;
		Engine::Vector3 contactPoint = boxPoint;
		if (kEpsilon < distance) {
			capsuleToBox = -delta * (1.0f / distance);
		} else {

			// 中心線が箱内なら最小の出口面を選ぶ
			const float halfExtents[3] = {queryBox.halfExtents.x, queryBox.halfExtents.y, queryBox.halfExtents.z};
			float minTranslation = (std::numeric_limits<float>::max)();
			Engine::Vector3 exitDirection{};
			Engine::Vector3 contactAnchor{};
			uint32_t contactAxis = 0;
			float contactSign = 1.0f;
			for (uint32_t i = 0; i < dimensionCount; ++i) {

				const float startCoordinate = Engine::Vector3::Dot(start - queryBox.center, queryBox.axes[i]);
				const float endCoordinate = Engine::Vector3::Dot(end - queryBox.center, queryBox.axes[i]);
				const float minCoordinate = (std::min)(startCoordinate, endCoordinate);
				const float maxCoordinate = (std::max)(startCoordinate, endCoordinate);
				const float positiveTranslation = halfExtents[i] + capsule.radius - minCoordinate;
				if (positiveTranslation < minTranslation) {
					minTranslation = positiveTranslation;
					exitDirection = queryBox.axes[i];
					contactAnchor = startCoordinate < endCoordinate ? start : end;
					contactAxis = i;
					contactSign = 1.0f;
				}

				const float negativeTranslation = maxCoordinate + capsule.radius + halfExtents[i];
				if (negativeTranslation < minTranslation) {
					minTranslation = negativeTranslation;
					exitDirection = -queryBox.axes[i];
					contactAnchor = startCoordinate < endCoordinate ? end : start;
					contactAxis = i;
					contactSign = -1.0f;
				}
			}

			capsuleToBox = -exitDirection;
			penetration = (std::max)(minTranslation, 0.0f);
			contactPoint = queryBox.center;
			for (uint32_t i = 0; i < 3; ++i) {

				const float coordinate = i == contactAxis
											 ? contactSign * halfExtents[i]
											 : Engine::Vector3::Dot(contactAnchor - queryBox.center, queryBox.axes[i]);
				const float clamped = i < dimensionCount ? std::clamp(coordinate, -halfExtents[i], halfExtents[i]) : coordinate;
				contactPoint += queryBox.axes[i] * clamped;
			}
		}

		// 法線と接触対象を入力順へ揃える
		if (capsuleIsA) {
			FillContact(capsule, box, outContact, capsuleToBox, penetration, contactPoint);
		} else {
			FillContact(box, capsule, outContact, -capsuleToBox, penetration, contactPoint);
		}
		return true;
	}
}
