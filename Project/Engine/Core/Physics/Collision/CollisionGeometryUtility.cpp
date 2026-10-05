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

		// 箱の各軸へ投影して範囲内へ収める
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
	Engine::Vector3 ClosestPointOnSegment(
		const Engine::Vector3& start, const Engine::Vector3& end, const Engine::Vector3& point) {

		const Engine::Vector3 segment = end - start;
		const float lengthSq = Engine::Vector3::Dot(segment, segment);
		if (lengthSq <= kEpsilon * kEpsilon) {
			return start;
		}
		const float t = std::clamp(Engine::Vector3::Dot(point - start, segment) / lengthSq, 0.0f, 1.0f);
		return start + segment * t;
	}

	// 2本の線分上で互いに最も近い点を求める
	void ClosestPointsOnSegments(const Engine::Vector3& startA, const Engine::Vector3& endA, const Engine::Vector3& startB,
		const Engine::Vector3& endB, Engine::Vector3& outPointA, Engine::Vector3& outPointB) {

		const Engine::Vector3 directionA = endA - startA;
		const Engine::Vector3 directionB = endB - startB;
		const Engine::Vector3 offset = startA - startB;
		const float lengthSqA = Engine::Vector3::Dot(directionA, directionA);
		const float lengthSqB = Engine::Vector3::Dot(directionB, directionB);
		const float offsetB = Engine::Vector3::Dot(directionB, offset);
		float tA = 0.0f;
		float tB = 0.0f;

		// 点へ縮んだ線分を先に処理する
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
					tA = std::clamp((directionsDot * offsetB - offsetA * lengthSqB) / denominator, 0.0f, 1.0f);
				}
				tB = (directionsDot * tA + offsetB) / lengthSqB;
				if (tB < 0.0f) {
					tB = 0.0f;
					tA = std::clamp(-offsetA / lengthSqA, 0.0f, 1.0f);
				} else if (1.0f < tB) {
					tB = 1.0f;
					tA = std::clamp((directionsDot - offsetA) / lengthSqA, 0.0f, 1.0f);
				}
			}
		}

		outPointA = startA + directionA * tA;
		outPointB = startB + directionB * tB;
	}

	// 線分とBox上で互いに最も近い点を求める
	void ClosestPointsSegmentBox(const Engine::Vector3& start, const Engine::Vector3& end,
		const Engine::CollisionShapeInstance& box, uint32_t dimensionCount, Engine::Vector3& outSegmentPoint,
		Engine::Vector3& outBoxPoint) {

		const Engine::Vector3 startOffset = start - box.center;
		const Engine::Vector3 segment = end - start;
		const float halfExtents[3] = {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z};
		float localStart[3]{};
		float localDirection[3]{};
		for (uint32_t i = 0; i < 3; ++i) {
			localStart[i] = Engine::Vector3::Dot(startOffset, box.axes[i]);
			localDirection[i] = Engine::Vector3::Dot(segment, box.axes[i]);
		}

		// 箱の境界を横切る位置で線分を区切る
		std::array<float, 8> boundaries{};
		uint32_t boundaryCount = 0;
		boundaries[boundaryCount++] = 0.0f;
		boundaries[boundaryCount++] = 1.0f;
		for (uint32_t i = 0; i < dimensionCount; ++i) {
			if (std::fabs(localDirection[i]) <= kEpsilon) {
				continue;
			}
			for (float boundary : {-halfExtents[i], halfExtents[i]}) {
				const float t = (boundary - localStart[i]) / localDirection[i];
				if (0.0f < t && t < 1.0f) {
					boundaries[boundaryCount++] = t;
				}
			}
		}
		std::sort(boundaries.begin(), boundaries.begin() + boundaryCount);

		// 各区間の最近点を比較する
		float bestT = 0.0f;
		float bestDistanceSq = (std::numeric_limits<float>::max)();
		auto evaluate = [&](float t) {
			float distanceSq = 0.0f;
			for (uint32_t i = 0; i < dimensionCount; ++i) {
				const float coordinate = localStart[i] + localDirection[i] * t;
				const float clamped = std::clamp(coordinate, -halfExtents[i], halfExtents[i]);
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

		// 最短距離の線分位置と箱上の点を公開する
		outSegmentPoint = start + segment * bestT;
		outBoxPoint = box.center;
		for (uint32_t i = 0; i < 3; ++i) {

			const float coordinate = localStart[i] + localDirection[i] * bestT;
			const float closest = i < dimensionCount ? std::clamp(coordinate, -halfExtents[i], halfExtents[i]) : coordinate;
			outBoxPoint += box.axes[i] * closest;
		}
	}

	// 接触情報をCollisionContactへ詰める、pointは実際の接触面の代表点
	void FillContact(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact, const Engine::Vector3& normal, float penetration, const Engine::Vector3& point) {

		// 接触対象と形状番号を設定する
		outContact.self = a.entity;
		outContact.other = b.entity;
		outContact.selfShapeIndex = a.shapeIndex;
		outContact.otherShapeIndex = b.shapeIndex;
		// 接触面とめり込みを設定する
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
}
