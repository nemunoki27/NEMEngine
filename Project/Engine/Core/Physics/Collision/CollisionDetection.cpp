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

	// 線分上でpointへ最も近い点を求める
	Engine::Vector3 ClosestPointOnSegment(const Engine::Vector3& start,
		const Engine::Vector3& end, const Engine::Vector3& point) {

		const Engine::Vector3 segment = end - start;
		const float lengthSq = Engine::Vector3::Dot(segment, segment);
		if (lengthSq <= kEpsilon * kEpsilon) {
			return start;
		}
		const float t = std::clamp(
			Engine::Vector3::Dot(point - start, segment) / lengthSq, 0.0f, 1.0f);
		return start + segment * t;
	}

	// 2本の線分上で互いに最も近い点を求める
	void ClosestPointsOnSegments(const Engine::Vector3& startA, const Engine::Vector3& endA,
		const Engine::Vector3& startB, const Engine::Vector3& endB,
		Engine::Vector3& outPointA, Engine::Vector3& outPointB) {

		const Engine::Vector3 directionA = endA - startA;
		const Engine::Vector3 directionB = endB - startB;
		const Engine::Vector3 offset = startA - startB;
		const float lengthSqA = Engine::Vector3::Dot(directionA, directionA);
		const float lengthSqB = Engine::Vector3::Dot(directionB, directionB);
		const float offsetB = Engine::Vector3::Dot(directionB, offset);
		float tA = 0.0f;
		float tB = 0.0f;

		if (lengthSqA <= kEpsilon * kEpsilon && lengthSqB <= kEpsilon * kEpsilon) {

			outPointA = startA;
			outPointB = startB;
			return;
		}
		if (lengthSqA <= kEpsilon * kEpsilon) {
			tB = std::clamp(offsetB / lengthSqB, 0.0f, 1.0f);
		} else {

			const float offsetA = Engine::Vector3::Dot(directionA, offset);
			if (lengthSqB <= kEpsilon * kEpsilon) {
				tA = std::clamp(-offsetA / lengthSqA, 0.0f, 1.0f);
			} else {

				const float directionsDot = Engine::Vector3::Dot(directionA, directionB);
				const float denominator = lengthSqA * lengthSqB - directionsDot * directionsDot;
				if (kEpsilon * kEpsilon < denominator) {
					tA = std::clamp(
						(directionsDot * offsetB - offsetA * lengthSqB) / denominator,
						0.0f, 1.0f);
				}
				tB = (directionsDot * tA + offsetB) / lengthSqB;
				if (tB < 0.0f) {
					tB = 0.0f;
					tA = std::clamp(-offsetA / lengthSqA, 0.0f, 1.0f);
				} else if (1.0f < tB) {
					tB = 1.0f;
					tA = std::clamp(
						(directionsDot - offsetA) / lengthSqA, 0.0f, 1.0f);
				}
			}
		}

		outPointA = startA + directionA * tA;
		outPointB = startB + directionB * tB;
	}

	// 線分とBox上で互いに最も近い点を求める
	void ClosestPointsSegmentBox(const Engine::Vector3& start, const Engine::Vector3& end,
		const Engine::CollisionShapeInstance& box, uint32_t dimensionCount,
		Engine::Vector3& outSegmentPoint, Engine::Vector3& outBoxPoint) {

		const Engine::Vector3 startOffset = start - box.center;
		const Engine::Vector3 segment = end - start;
		const float halfExtents[3] = {
			box.halfExtents.x, box.halfExtents.y, box.halfExtents.z
		};
		float localStart[3]{};
		float localDirection[3]{};
		for (uint32_t i = 0; i < 3; ++i) {
			localStart[i] = Engine::Vector3::Dot(startOffset, box.axes[i]);
			localDirection[i] = Engine::Vector3::Dot(segment, box.axes[i]);
		}

		std::array<float, 8> boundaries{};
		uint32_t boundaryCount = 0;
		boundaries[boundaryCount++] = 0.0f;
		boundaries[boundaryCount++] = 1.0f;
		for (uint32_t i = 0; i < dimensionCount; ++i) {
			if (std::fabs(localDirection[i]) <= kEpsilon) {
				continue;
			}
			for (float boundary : { -halfExtents[i], halfExtents[i] }) {
				const float t = (boundary - localStart[i]) / localDirection[i];
				if (0.0f < t && t < 1.0f) {
					boundaries[boundaryCount++] = t;
				}
			}
		}
		std::sort(boundaries.begin(), boundaries.begin() + boundaryCount);

		float bestT = 0.0f;
		float bestDistanceSq = (std::numeric_limits<float>::max)();
		auto evaluate = [&](float t) {

			float distanceSq = 0.0f;
			for (uint32_t i = 0; i < dimensionCount; ++i) {
				const float coordinate = localStart[i] + localDirection[i] * t;
				const float clamped = std::clamp(
					coordinate, -halfExtents[i], halfExtents[i]);
				const float distance = coordinate - clamped;
				distanceSq += distance * distance;
			}
			if (distanceSq < bestDistanceSq) {
				bestDistanceSq = distanceSq;
				bestT = t;
			}
		};

		for (uint32_t i = 0; i < boundaryCount; ++i) {
			evaluate(boundaries[i]);
		}
		for (uint32_t i = 0; i + 1 < boundaryCount; ++i) {

			const float begin = boundaries[i];
			const float endT = boundaries[i + 1];
			const float middle = (begin + endT) * 0.5f;
			float numerator = 0.0f;
			float denominator = 0.0f;
			for (uint32_t axis = 0; axis < dimensionCount; ++axis) {

				const float coordinate = localStart[axis] + localDirection[axis] * middle;
				float boundary = 0.0f;
				if (coordinate < -halfExtents[axis]) {
					boundary = -halfExtents[axis];
				} else if (halfExtents[axis] < coordinate) {
					boundary = halfExtents[axis];
				} else {
					continue;
				}
				numerator += localDirection[axis] * (localStart[axis] - boundary);
				denominator += localDirection[axis] * localDirection[axis];
			}
			if (kEpsilon * kEpsilon < denominator) {
				evaluate(std::clamp(-numerator / denominator, begin, endT));
			}
		}

		outSegmentPoint = start + segment * bestT;
		outBoxPoint = box.center;
		for (uint32_t i = 0; i < 3; ++i) {

			const float coordinate = localStart[i] + localDirection[i] * bestT;
			const float closest = i < dimensionCount ?
				std::clamp(coordinate, -halfExtents[i], halfExtents[i]) : coordinate;
			outBoxPoint += box.axes[i] * closest;
		}
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

	// 2D判定用にZ成分を除く
	Engine::Vector3 Flatten2D(Engine::Vector3 value) {

		value.z = 0.0f;
		return value;
	}

	// カプセルと円または球の衝突判定
	bool TestCapsuleSphere(const Engine::CollisionShapeInstance& capsule,
		const Engine::CollisionShapeInstance& sphere, bool capsuleIsA,
		bool is2D, Engine::CollisionContact& outContact) {

		const Engine::Vector3 segmentStart = is2D ?
			Flatten2D(capsule.segmentStart) : capsule.segmentStart;
		const Engine::Vector3 segmentEnd = is2D ?
			Flatten2D(capsule.segmentEnd) : capsule.segmentEnd;
		const Engine::Vector3 sphereCenter = is2D ?
			Flatten2D(sphere.center) : sphere.center;
		const Engine::Vector3 closest = ClosestPointOnSegment(
			segmentStart, segmentEnd, sphereCenter);
		const Engine::Vector3 delta = sphereCenter - closest;
		const float distanceSq = Engine::Vector3::Dot(delta, delta);
		const float radius = capsule.radius + sphere.radius;
		if (radius * radius < distanceSq) {
			return false;
		}

		const float distance = std::sqrt(distanceSq);
		Engine::Vector3 fallback = sphereCenter - (is2D ?
			Flatten2D(capsule.center) : capsule.center);
		fallback = Engine::Vector3::NormalizeOr(
			fallback, Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 capsuleToSphere = Engine::Vector3::NormalizeOr(delta, fallback);
		const Engine::Vector3 point = closest + capsuleToSphere * capsule.radius;
		if (capsuleIsA) {
			FillContact(capsule, sphere, outContact,
				capsuleToSphere, radius - distance, point);
		} else {
			FillContact(sphere, capsule, outContact,
				-capsuleToSphere, radius - distance, point);
		}
		return true;
	}

	// カプセル同士の衝突判定
	bool TestCapsuleCapsule(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, bool is2D,
		Engine::CollisionContact& outContact) {

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
		Engine::Vector3 fallback = (is2D ? Flatten2D(b.center) : b.center) -
			(is2D ? Flatten2D(a.center) : a.center);
		fallback = Engine::Vector3::NormalizeOr(
			fallback, Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 normal = Engine::Vector3::NormalizeOr(delta, fallback);
		FillContact(a, b, outContact, normal,
			radius - distance, pointA + normal * a.radius);
		return true;
	}

	// カプセルと回転Boxの衝突判定、dimensionCountが2ならQuadとして扱う
	bool TestCapsuleBox(const Engine::CollisionShapeInstance& capsule,
		const Engine::CollisionShapeInstance& box, bool capsuleIsA,
		uint32_t dimensionCount, Engine::CollisionContact& outContact) {

		const bool is2D = dimensionCount == 2;
		const Engine::Vector3 start = is2D ?
			Flatten2D(capsule.segmentStart) : capsule.segmentStart;
		const Engine::Vector3 end = is2D ?
			Flatten2D(capsule.segmentEnd) : capsule.segmentEnd;
		Engine::CollisionShapeInstance queryBox = box;
		if (is2D) {
			queryBox.center = Flatten2D(queryBox.center);
			queryBox.axes[0] = Engine::Vector3::NormalizeOr(
				Flatten2D(queryBox.axes[0]), Engine::Vector3(1.0f, 0.0f, 0.0f));
			queryBox.axes[1] = Engine::Vector3::NormalizeOr(
				Flatten2D(queryBox.axes[1]), Engine::Vector3(0.0f, 1.0f, 0.0f));
			queryBox.axes[2] = Engine::Vector3(0.0f, 0.0f, 1.0f);
		}

		Engine::Vector3 segmentPoint{};
		Engine::Vector3 boxPoint{};
		ClosestPointsSegmentBox(
			start, end, queryBox, dimensionCount, segmentPoint, boxPoint);
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

			const float halfExtents[3] = {
				queryBox.halfExtents.x, queryBox.halfExtents.y, queryBox.halfExtents.z
			};
			float minTranslation = (std::numeric_limits<float>::max)();
			Engine::Vector3 exitDirection{};
			Engine::Vector3 contactAnchor{};
			uint32_t contactAxis = 0;
			float contactSign = 1.0f;
			for (uint32_t i = 0; i < dimensionCount; ++i) {

				const float startCoordinate = Engine::Vector3::Dot(
					start - queryBox.center, queryBox.axes[i]);
				const float endCoordinate = Engine::Vector3::Dot(
					end - queryBox.center, queryBox.axes[i]);
				const float minCoordinate = (std::min)(startCoordinate, endCoordinate);
				const float maxCoordinate = (std::max)(startCoordinate, endCoordinate);
				const float positiveTranslation =
					halfExtents[i] + capsule.radius - minCoordinate;
				if (positiveTranslation < minTranslation) {
					minTranslation = positiveTranslation;
					exitDirection = queryBox.axes[i];
					contactAnchor = startCoordinate < endCoordinate ? start : end;
					contactAxis = i;
					contactSign = 1.0f;
				}

				const float negativeTranslation =
					maxCoordinate + capsule.radius + halfExtents[i];
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

				const float coordinate = i == contactAxis ?
					contactSign * halfExtents[i] :
					Engine::Vector3::Dot(
						contactAnchor - queryBox.center, queryBox.axes[i]);
				const float clamped = i < dimensionCount ?
					std::clamp(coordinate, -halfExtents[i], halfExtents[i]) : coordinate;
				contactPoint += queryBox.axes[i] * clamped;
			}
		}

		if (capsuleIsA) {
			FillContact(capsule, box, outContact,
				capsuleToBox, penetration, contactPoint);
		} else {
			FillContact(box, capsule, outContact,
				-capsuleToBox, penetration, contactPoint);
		}
		return true;
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

	return type == ColliderShapeType::Circle2D ||
		type == ColliderShapeType::Quad2D ||
		type == ColliderShapeType::Capsule2D;
}

bool Engine::IsCollisionShape3D(ColliderShapeType type) {

	return type == ColliderShapeType::Sphere3D ||
		type == ColliderShapeType::AABB3D ||
		type == ColliderShapeType::OBB3D ||
		type == ColliderShapeType::Capsule3D;
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
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Circle2D) {
		return TestCapsuleSphere(a, b, true, true, outContact);
	}
	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleSphere(b, a, false, true, outContact);
	}
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Quad2D) {
		return TestCapsuleBox(a, b, true, 2, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleBox(b, a, false, 2, outContact);
	}
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleCapsule(a, b, true, outContact);
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
	if (a.type == ColliderShapeType::Capsule3D && b.type == ColliderShapeType::Sphere3D) {
		return TestCapsuleSphere(a, b, true, false, outContact);
	}
	if (a.type == ColliderShapeType::Sphere3D && b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleSphere(b, a, false, false, outContact);
	}
	if (a.type == ColliderShapeType::Capsule3D &&
		(b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestCapsuleBox(a, b, true, 3, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) &&
		b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleBox(b, a, false, 3, outContact);
	}
	if (a.type == ColliderShapeType::Capsule3D && b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleCapsule(a, b, false, outContact);
	}
	return false;
}
