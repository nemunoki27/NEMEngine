#include "CollisionRaycast.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CollisionRaycast classMethods
//============================================================================
bool Engine::CollisionRaycast::RayVsSphere(const Ray& ray, const Vector3& center, float radius,
	float maxDistance, float& outDistance, Vector3& outNormal) {

	const Vector3 toOrigin = ray.origin - center;
	const float b = Vector3::Dot(toOrigin, ray.direction);
	const float c = Vector3::Dot(toOrigin, toOrigin) - radius * radius;

	// 始点が内部なら距離0でヒットさせる、法線はレイと逆向きにする
	if (c <= 0.0f) {
		outDistance = 0.0f;
		outNormal = -ray.direction;
		return true;
	}

	const float discriminant = b * b - c;
	if (discriminant < 0.0f) {
		return false;
	}

	const float distance = -b - std::sqrt(discriminant);
	if (distance < 0.0f || maxDistance < distance) {
		return false;
	}
	outDistance = distance;
	outNormal = Vector3::NormalizeOr((ray.origin + ray.direction * distance) - center, -ray.direction);
	return true;
}

bool Engine::CollisionRaycast::RayVsCapsule(const Ray& ray,
	const CollisionShapeInstance& capsule, float maxDistance,
	float& outDistance, Vector3& outNormal) {

	const Vector3 axis = capsule.segmentEnd - capsule.segmentStart;
	const float axisLengthSq = Vector3::Dot(axis, axis);
	if (axisLengthSq <= 0.00000001f) {
		return RayVsSphere(ray, capsule.center, capsule.radius,
			maxDistance, outDistance, outNormal);
	}

	// 始点がカプセル内部なら距離0でヒットさせる
	const float closestT = std::clamp(
		Vector3::Dot(ray.origin - capsule.segmentStart, axis) / axisLengthSq,
		0.0f, 1.0f);
	const Vector3 closest = capsule.segmentStart + axis * closestT;
	const Vector3 fromAxis = ray.origin - closest;
	if (Vector3::Dot(fromAxis, fromAxis) <= capsule.radius * capsule.radius) {
		outDistance = 0.0f;
		outNormal = -ray.direction;
		return true;
	}

	float bestDistance = maxDistance;
	Vector3 bestNormal{};
	bool hit = false;

	// 無限円柱との交点が両端間にある場合は側面へのヒット
	const Vector3 toOrigin = ray.origin - capsule.segmentStart;
	const float axisRay = Vector3::Dot(axis, ray.direction);
	const float axisOrigin = Vector3::Dot(axis, toOrigin);
	const float rayOrigin = Vector3::Dot(ray.direction, toOrigin);
	const float originLengthSq = Vector3::Dot(toOrigin, toOrigin);
	const float a = axisLengthSq - axisRay * axisRay;
	const float b = axisLengthSq * rayOrigin - axisOrigin * axisRay;
	const float c = axisLengthSq * originLengthSq - axisOrigin * axisOrigin -
		capsule.radius * capsule.radius * axisLengthSq;
	const float discriminant = b * b - a * c;
	if (0.00000001f < std::fabs(a) && 0.0f <= discriminant) {

		const float distance = (-b - std::sqrt(discriminant)) / a;
		const float axisDistance = axisOrigin + distance * axisRay;
		if (0.0f <= distance && distance <= maxDistance &&
			0.0f <= axisDistance && axisDistance <= axisLengthSq) {

			const Vector3 point = ray.origin + ray.direction * distance;
			const Vector3 pointOnAxis = capsule.segmentStart +
				axis * (axisDistance / axisLengthSq);
			bestDistance = distance;
			bestNormal = Vector3::NormalizeOr(point - pointOnAxis, -ray.direction);
			hit = true;
		}
	}

	// 両端の半球を含めた全候補から最短ヒットを選ぶ
	for (const Vector3& center : { capsule.segmentStart, capsule.segmentEnd }) {

		float distance = 0.0f;
		Vector3 normal{};
		if (RayVsSphere(ray, center, capsule.radius,
			bestDistance, distance, normal)) {

			bestDistance = distance;
			bestNormal = normal;
			hit = true;
		}
	}
	if (!hit) {
		return false;
	}
	outDistance = bestDistance;
	outNormal = bestNormal;
	return true;
}

bool Engine::CollisionRaycast::RayVsOBB(const Ray& ray, const CollisionShapeInstance& box,
	float maxDistance, float& outDistance, Vector3& outNormal) {

	// レイを箱のローカル軸空間へ写してslab法で判定する
	const Vector3 toOrigin = ray.origin - box.center;
	const float halfExtents[3] = { box.halfExtents.x, box.halfExtents.y, box.halfExtents.z };
	float enterDistance = 0.0f;
	float exitDistance = maxDistance;
	int32_t enterAxis = -1;
	float enterSign = 1.0f;
	bool inside = true;

	for (int32_t axis = 0; axis < 3; ++axis) {

		const float localOrigin = Vector3::Dot(toOrigin, box.axes[axis]);
		const float localDirection = Vector3::Dot(ray.direction, box.axes[axis]);
		const float halfExtent = halfExtents[axis];

		if (std::fabs(localDirection) <= 0.000001f) {

			// 軸と平行なレイはslab外なら不ヒット
			if (localOrigin < -halfExtent || halfExtent < localOrigin) {
				return false;
			}
			continue;
		}

		float nearDistance = (-halfExtent - localOrigin) / localDirection;
		float farDistance = (halfExtent - localOrigin) / localDirection;
		float sign = -1.0f;
		if (farDistance < nearDistance) {
			std::swap(nearDistance, farDistance);
			sign = 1.0f;
		}
		if (localOrigin < -halfExtent || halfExtent < localOrigin) {
			inside = false;
		}
		if (enterDistance < nearDistance) {
			enterDistance = nearDistance;
			enterAxis = axis;
			enterSign = sign;
		}
		exitDistance = (std::min)(exitDistance, farDistance);
		if (exitDistance < enterDistance) {
			return false;
		}
	}

	// 始点が内部なら距離0でヒットさせる
	if (inside) {
		outDistance = 0.0f;
		outNormal = -ray.direction;
		return true;
	}
	if (enterAxis < 0 || maxDistance < enterDistance) {
		return false;
	}
	outDistance = enterDistance;
	outNormal = box.axes[enterAxis] * enterSign;
	return true;
}

bool Engine::CollisionRaycast::RayVsTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2,
	float maxDistance, float& outDistance, Vector3& outNormal) {

	// Moller-Trumbore法、両面判定
	const Vector3 edge1 = v1 - v0;
	const Vector3 edge2 = v2 - v0;
	const Vector3 crossDirection = Vector3::Cross(ray.direction, edge2);
	const float determinant = Vector3::Dot(edge1, crossDirection);
	if (std::fabs(determinant) <= 0.000001f) {
		return false;
	}

	const float inverseDeterminant = 1.0f / determinant;
	const Vector3 toOrigin = ray.origin - v0;
	const float u = Vector3::Dot(toOrigin, crossDirection) * inverseDeterminant;
	if (u < 0.0f || 1.0f < u) {
		return false;
	}
	const Vector3 crossOrigin = Vector3::Cross(toOrigin, edge1);
	const float v = Vector3::Dot(ray.direction, crossOrigin) * inverseDeterminant;
	if (v < 0.0f || 1.0f < u + v) {
		return false;
	}

	const float distance = Vector3::Dot(edge2, crossOrigin) * inverseDeterminant;
	if (distance < 0.0f || maxDistance < distance) {
		return false;
	}

	// 法線はレイへ向いた側の面を返す
	Vector3 normal = Vector3::NormalizeOr(Vector3::Cross(edge1, edge2), Vector3(0.0f, 1.0f, 0.0f));
	if (0.0f < Vector3::Dot(normal, ray.direction)) {
		normal = -normal;
	}
	outDistance = distance;
	outNormal = normal;
	return true;
}

bool Engine::CollisionRaycast::RayVsShape(const Ray& ray, const CollisionShapeInstance& shape,
	float maxDistance, float& outDistance, Vector3& outNormal) {

	switch (shape.type) {
	case ColliderShapeType::Sphere3D:
		return RayVsSphere(ray, shape.center, shape.radius, maxDistance, outDistance, outNormal);
	case ColliderShapeType::Capsule3D:
		return RayVsCapsule(ray, shape, maxDistance, outDistance, outNormal);
	case ColliderShapeType::AABB3D:
	case ColliderShapeType::OBB3D:
		return RayVsOBB(ray, shape, maxDistance, outDistance, outNormal);
	default:
		return false;
	}
}
