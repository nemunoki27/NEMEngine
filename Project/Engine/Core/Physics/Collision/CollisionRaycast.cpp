#include "CollisionRaycast.h"

//============================================================================
//	include
//============================================================================

// c++
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
	case ColliderShapeType::AABB3D:
	case ColliderShapeType::OBB3D:
		return RayVsOBB(ray, shape, maxDistance, outDistance, outNormal);
	default:
		return false;
	}
}