#include "CollisionDetection.h"

// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <vector>

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

	// 箱の各軸の半幅を取得する
	float BoxExtent(const Engine::CollisionShapeInstance& box, uint32_t axis) {

		return axis == 0 ? box.halfExtents.x : axis == 1 ? box.halfExtents.y : box.halfExtents.z;
	}

	// 有効な立体の箱だけを隣接判定へ渡す
	bool IsSurfaceBox(const Engine::CollisionShapeInstance& box) {

		return (box.type == Engine::ColliderShapeType::AABB3D || box.type == Engine::ColliderShapeType::OBB3D) &&
			!box.trigger && box.halfExtents.x > 0.0f && box.halfExtents.y > 0.0f && box.halfExtents.z > 0.0f &&
			std::isfinite(box.halfExtents.x + box.halfExtents.y + box.halfExtents.z) &&
			std::isfinite(box.center.x) && std::isfinite(box.center.y) && std::isfinite(box.center.z);
	}

	// 軸の入れ替えと符号反転を含め、向きが揃った箱か確認する
	bool AlignedBoxAxes(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b) {

		for (const auto& axis : a.axes) {
			bool aligned = false;
			for (const auto& other : b.axes) {
				aligned |= AbsDot(axis, other) > 0.999999f &&
					Engine::Vector3::Cross(axis, other).Length() <= 0.00001f;
			}
			if (!aligned) {
				return false;
			}
		}
		return true;
	}

	// 各軸の負側と正側を交互に面番号へ割り当てる
	uint8_t BoxFaceBit(uint32_t axis, bool positive) {

		return static_cast<uint8_t>(1u << (axis * 2 + (positive ? 1 : 0)));
	}

	// normal方向を向く面が内部面か確認する
	bool IsInternalBoxFace(const Engine::CollisionShapeInstance& box, uint8_t faces,
		const Engine::Vector3& normal) {

		if (!faces) {
			return false;
		}
		for (uint32_t i = 0; i < 3; ++i) {
			const float dot = Engine::Vector3::Dot(box.axes[i], normal);
			if (std::fabs(dot) > 0.999999f && (faces & BoxFaceBit(i, dot > 0.0f))) {
				return true;
			}
		}
		return false;
	}

	// 継ぎ目の代替面は相手の中心側へ露出する面だけにする
	bool IsFacingBoxSurface(const Engine::CollisionShapeInstance& box,
		const Engine::Vector3& otherCenter, const Engine::Vector3& normal) {

		const float distance = Engine::Vector3::Dot(otherCenter - box.center, normal);
		return distance >= ProjectBoxRadius(box, normal) - kEpsilon;
	}

	// 隣の箱が面全体を覆う場合だけ内部面にする
	uint8_t CoveredBoxFaces(const Engine::CollisionShapeInstance& box,
		const Engine::CollisionShapeInstance& neighbor) {

		const float size = 2.0f * (std::min)({ box.halfExtents.x, box.halfExtents.y, box.halfExtents.z,
			neighbor.halfExtents.x, neighbor.halfExtents.y, neighbor.halfExtents.z });
		const float tolerance = (std::min)(size * 0.00001f, 0.0001f);
		const auto delta = neighbor.center - box.center;
		uint8_t faces = 0;
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const float distance = Engine::Vector3::Dot(delta, box.axes[axis]);
			const float radius = BoxExtent(box, axis) + ProjectBoxRadius(neighbor, box.axes[axis]);
			if (std::fabs(std::fabs(distance) - radius) > tolerance) {
				continue;
			}
			bool covered = true;
			for (uint32_t tangent = 0; tangent < 3; ++tangent) {
				if (axis != tangent && std::fabs(Engine::Vector3::Dot(delta, box.axes[tangent])) +
					BoxExtent(box, tangent) > ProjectBoxRadius(neighbor, box.axes[tangent]) + tolerance) {
					covered = false;
					break;
				}
			}
			if (covered) {
				faces |= BoxFaceBit(axis, distance > 0.0f);
			}
		}
		return faces;
	}

	// 接触点を採用した外側の面へ揃える
	Engine::Vector3 PointOnBoxFace(const Engine::CollisionShapeInstance& box,
		const Engine::Vector3& point, const Engine::Vector3& normal) {

		auto result = ClosestPointOnBox(box, point);
		for (uint32_t i = 0; i < 3; ++i) {
			const float dot = Engine::Vector3::Dot(box.axes[i], normal);
			if (std::fabs(dot) > 0.999999f) {
				const float target = dot > 0.0f ? BoxExtent(box, i) : -BoxExtent(box, i);
				result += box.axes[i] * (target - Engine::Vector3::Dot(result - box.center, box.axes[i]));
				break;
			}
		}
		return result;
	}

	// AABB3D / OBB3D同士の衝突判定
	bool TestBoxBox3D(const Engine::CollisionShapeInstance& a,
		const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact,
		uint8_t facesA = 0, uint8_t facesB = 0) {

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
			// 内部面の軸も分離判定には使い、押し戻し候補からだけ除外する
			if (facesA | facesB) {
				const auto normal = Engine::Vector3::Dot(centerDelta, axis) < 0.0f ? -axis : axis;
				if (IsInternalBoxFace(a, facesA, normal) || IsInternalBoxFace(b, facesB, -normal)) {
					continue;
				}
				// 内部面の代わりに、箱の奥を通って反対側やZ端へ押し出さない
				if ((facesA && !IsFacingBoxSurface(a, b.center, normal)) ||
					(facesB && !IsFacingBoxSurface(b, a.center, -normal))) {
					continue;
				}
			}
			if (penetration < minPenetration) {
				minPenetration = penetration;
				bestAxis = axis;
			}
		}

		if (Engine::Vector3::Dot(bestAxis, centerDelta) < 0.0f) {
			bestAxis = -bestAxis;
		}
		if (minPenetration == (std::numeric_limits<float>::max)()) {
			return false;
		}
		const auto point = facesB ? PointOnBoxFace(b, a.center, -bestAxis) :
			facesA ? PointOnBoxFace(a, b.center, bestAxis) : ClosestPointOnBox(b, a.center);
		FillContact(a, b, outContact, bestAxis, minPenetration, point);
		return true;
	}
}

//============================================================================
//	CollisionDetection functions
//============================================================================
size_t Engine::BuildBoxInternalFaces(std::span<CollisionBoxSurface> surfaces) {

	struct Bounds {

		size_t index;
		std::array<float, 3> min;
		std::array<float, 3> max;
	};
	const Vector3 axes[] = { Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) };
	std::vector<Bounds> bounds;
	bounds.reserve(surfaces.size());
	std::array<float, 3> minCenter{ FLT_MAX, FLT_MAX, FLT_MAX };
	std::array<float, 3> maxCenter{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (size_t i = 0; i < surfaces.size(); ++i) {
		auto& surface = surfaces[i];
		surface.internalFaces = 0;
		if (!surface.shape || !IsSurfaceBox(*surface.shape)) {
			continue;
		}
		Bounds entry{ i, {}, {} };
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const float center = Vector3::Dot(surface.shape->center, axes[axis]);
			const float radius = ProjectBoxRadius(*surface.shape, axes[axis]);
			entry.min[axis] = center - radius;
			entry.max[axis] = center + radius;
			minCenter[axis] = (std::min)(minCenter[axis], center);
			maxCenter[axis] = (std::max)(maxCenter[axis], center);
		}
		bounds.push_back(entry);
	}
	if (bounds.size() < 2) {
		return 0;
	}
	// 最も広がる軸で走査し、離れた列やフィルムを候補から外す
	uint32_t sweepAxis = 0;
	for (uint32_t axis = 1; axis < 3; ++axis) {
		if (maxCenter[axis] - minCenter[axis] > maxCenter[sweepAxis] - minCenter[sweepAxis]) {
			sweepAxis = axis;
		}
	}
	std::sort(bounds.begin(), bounds.end(), [sweepAxis](const Bounds& a, const Bounds& b) {
		return a.min[sweepAxis] < b.min[sweepAxis];
		});
	size_t comparisons = 0;
	for (size_t i = 0; i < bounds.size(); ++i) {
		for (size_t j = i + 1; j < bounds.size(); ++j) {
			const auto& aBounds = bounds[i];
			const auto& bBounds = bounds[j];
			if (bBounds.min[sweepAxis] > aBounds.max[sweepAxis] + 0.0001f) {
				break;
			}
			++comparisons;
			auto& a = surfaces[aBounds.index];
			auto& b = surfaces[bBounds.index];
			if (a.typeMask != b.typeMask) {
				continue;
			}
			bool nearby = true;
			for (uint32_t axis = 0; axis < 3; ++axis) {
				if (aBounds.min[axis] > bBounds.max[axis] + 0.0001f ||
					bBounds.min[axis] > aBounds.max[axis] + 0.0001f) {
					nearby = false;
					break;
				}
			}
			if (!nearby || !AlignedBoxAxes(*a.shape, *b.shape)) {
				continue;
			}
			a.internalFaces |= CoveredBoxFaces(*a.shape, *b.shape);
			b.internalFaces |= CoveredBoxFaces(*b.shape, *a.shape);
		}
	}
	return comparisons;
}

bool Engine::TestCollisionWithBoxInternalFaces(const CollisionShapeInstance& a, const CollisionShapeInstance& b,
	uint8_t facesA, uint8_t facesB, CollisionContact& outContact) {

	if (!TestCollision(a, b, outContact)) {
		return false;
	}
	// 通常の床や壁の接触点と法線は変更しない
	if (!(facesA | facesB) || !IsSurfaceBox(a) || !IsSurfaceBox(b) || !AlignedBoxAxes(a, b) ||
		(!IsInternalBoxFace(a, facesA, outContact.normal) &&
			!IsInternalBoxFace(b, facesB, -outContact.normal))) {
		return true;
	}
	CollisionContact corrected{};
	if (TestBoxBox3D(a, b, corrected, facesA, facesB)) {
		outContact = corrected;
	}
	// 安全な代替面がない埋まり込みでも、接触と通常の押し戻しは失わない
	return true;
}

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
