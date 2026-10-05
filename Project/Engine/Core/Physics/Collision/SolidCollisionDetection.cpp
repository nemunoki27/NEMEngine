#include "CollisionDetectionDetail.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <vector>

namespace Engine::CollisionDetectionDetail {

	// Sphere3D同士の衝突判定
	bool TestSphereSphere(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact) {

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
	bool TestSphereBox(const Engine::CollisionShapeInstance& sphere, const Engine::CollisionShapeInstance& box, bool sphereIsA,
		Engine::CollisionContact& outContact) {

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

		return AbsDot(box.axes[0], axis) * box.halfExtents.x + AbsDot(box.axes[1], axis) * box.halfExtents.y +
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
			   std::isfinite(box.halfExtents.x + box.halfExtents.y + box.halfExtents.z) && std::isfinite(box.center.x) &&
			   std::isfinite(box.center.y) && std::isfinite(box.center.z);
	}

	// 軸の入れ替えと符号反転を含め、向きが揃った箱か確認する
	bool AlignedBoxAxes(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b) {

		for (const auto& axis : a.axes) {
			bool aligned = false;
			for (const auto& other : b.axes) {
				aligned |= AbsDot(axis, other) > 0.999999f && Engine::Vector3::Cross(axis, other).Length() <= 0.00001f;
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
	bool IsInternalBoxFace(const Engine::CollisionShapeInstance& box, uint8_t faces, const Engine::Vector3& normal) {

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
	bool IsFacingBoxSurface(
		const Engine::CollisionShapeInstance& box, const Engine::Vector3& otherCenter, const Engine::Vector3& normal) {

		const float distance = Engine::Vector3::Dot(otherCenter - box.center, normal);
		return distance >= ProjectBoxRadius(box, normal) - kEpsilon;
	}

	// 隣の箱が面全体を覆う場合だけ内部面にする
	uint8_t CoveredBoxFaces(const Engine::CollisionShapeInstance& box, const Engine::CollisionShapeInstance& neighbor) {

		const float size = 2.0f * (std::min)({box.halfExtents.x, box.halfExtents.y, box.halfExtents.z, neighbor.halfExtents.x,
									  neighbor.halfExtents.y, neighbor.halfExtents.z});
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
				if (axis != tangent && std::fabs(Engine::Vector3::Dot(delta, box.axes[tangent])) + BoxExtent(box, tangent) >
										   ProjectBoxRadius(neighbor, box.axes[tangent]) + tolerance) {
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
	Engine::Vector3 PointOnBoxFace(
		const Engine::CollisionShapeInstance& box, const Engine::Vector3& point, const Engine::Vector3& normal) {

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
	bool TestBoxBox3D(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact, uint8_t facesA, uint8_t facesB) {

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
		const auto point = facesB	? PointOnBoxFace(b, a.center, -bestAxis)
						   : facesA ? PointOnBoxFace(a, b.center, bestAxis)
									: ClosestPointOnBox(b, a.center);
		FillContact(a, b, outContact, bestAxis, minPenetration, point);
		return true;
	}
}

using namespace Engine::CollisionDetectionDetail;

//============================================================================
size_t Engine::BuildBoxInternalFaces(std::span<CollisionBoxSurface> surfaces) {

	// 広域判定用の軸範囲
	struct Bounds {

		size_t index; // 元の形状位置
		std::array<float, 3> min; // 各軸の最小値
		std::array<float, 3> max; // 各軸の最大値
	};
	const Vector3 axes[] = {Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
	std::vector<Bounds> bounds;
	bounds.reserve(surfaces.size());
	std::array<float, 3> minCenter{FLT_MAX, FLT_MAX, FLT_MAX};
	std::array<float, 3> maxCenter{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	// 有効な支持面だけ境界範囲へ変換する
	for (size_t i = 0; i < surfaces.size(); ++i) {
		auto& surface = surfaces[i];
		surface.internalFaces = 0;
		if (!surface.shape || !IsSurfaceBox(*surface.shape)) {
			continue;
		}
		Bounds entry{i, {}, {}};
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
	std::sort(bounds.begin(), bounds.end(),
		[sweepAxis](const Bounds& a, const Bounds& b) { return a.min[sweepAxis] < b.min[sweepAxis]; });
	// 広域候補の向きと被覆面を比較する
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
				if (aBounds.min[axis] > bBounds.max[axis] + 0.0001f || bBounds.min[axis] > aBounds.max[axis] + 0.0001f) {
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

bool Engine::TestCollisionWithBoxInternalFaces(const CollisionShapeInstance& a, const CollisionShapeInstance& b, uint8_t facesA,
	uint8_t facesB, CollisionContact& outContact) {

	if (!TestCollision(a, b, outContact)) {
		return false;
	}
	// 通常の床や壁の接触点と法線は変更しない
	if (!(facesA | facesB) || !IsSurfaceBox(a) || !IsSurfaceBox(b) || !AlignedBoxAxes(a, b) ||
		(!IsInternalBoxFace(a, facesA, outContact.normal) && !IsInternalBoxFace(b, facesB, -outContact.normal))) {
		return true;
	}
	CollisionContact corrected{};
	if (TestBoxBox3D(a, b, corrected, facesA, facesB)) {
		outContact = corrected;
	}
	// 安全な代替面がない埋まり込みでも、接触と通常の押し戻しは失わない
	return true;
}
