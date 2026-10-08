#include "CollisionSupport.h"

//============================================================================
//	include
//============================================================================
#include "CollisionBodyUtility.h"
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::CollisionBodyUtility;

namespace Engine::CollisionSupport {

	// 支持面として扱う上向き法線の下限
	constexpr float kSupportNormalMin = 0.5f;
	// 自身と支持面が安定して接しているとみなす軸一致率
	constexpr float kSupportAlignmentMin = 0.98f;
	// 支持範囲の境界で数値誤差による転倒を防ぐ余白
	constexpr float kSupportEdgeTolerance = 0.001f;
	// 接地後に停止扱いにする角速度
	constexpr float kRestingAngularSpeed = 0.001f;

	// 3DのBox形状か
	bool IsBoxShape3D(const Engine::CollisionShapeInstance* shape) {

		return shape && (shape->type == Engine::ColliderShapeType::AABB3D || shape->type == Engine::ColliderShapeType::OBB3D);
	}

	// 指定法線と最も一致するBox軸を返す
	uint32_t FindClosestBoxAxis(
		const Engine::CollisionShapeInstance& shape, const Engine::Vector3& normal, float& outAlignment) {

		uint32_t result = 0;
		outAlignment = std::fabs(Engine::Vector3::Dot(shape.axes[0], normal));
		for (uint32_t i = 1; i < 3; ++i) {

			const float alignment = std::fabs(Engine::Vector3::Dot(shape.axes[i], normal));
			if (outAlignment < alignment) {
				result = i;
				outAlignment = alignment;
			}
		}
		return result;
	}

	// Transform回転の軸から指定法線に最も近い軸を返す
	uint32_t FindClosestTransformAxis(const Engine::TransformComponent& transform, const Engine::Vector3& normal,
		float& outAlignment, Engine::Vector3& outAxis) {

		const Engine::Matrix4x4 rotation = Engine::Quaternion::MakeRotateMatrix(transform.localRotation);
		const Engine::Vector3 axes[3] = {
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(1.0f, 0.0f, 0.0f), rotation),
				Engine::Vector3(1.0f, 0.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(0.0f, 1.0f, 0.0f), rotation),
				Engine::Vector3(0.0f, 1.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(0.0f, 0.0f, 1.0f), rotation),
				Engine::Vector3(0.0f, 0.0f, 1.0f)),
		};

		uint32_t result = 0;
		outAlignment = std::fabs(Engine::Vector3::Dot(axes[0], normal));
		for (uint32_t i = 1; i < 3; ++i) {

			const float alignment = std::fabs(Engine::Vector3::Dot(axes[i], normal));
			if (outAlignment < alignment) {
				result = i;
				outAlignment = alignment;
			}
		}
		outAxis = axes[result];
		return result;
	}

	// 支持面へ最も近いBox面を正確に揃える
	void SettleSupportedRotation(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& normal,
		const Engine::CollisionShapeInstance* selfShape) {

		if (!selfShape || (!IsBoxShape3D(selfShape) && selfShape->type != Engine::ColliderShapeType::Quad2D) ||
			!world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}

		auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		float alignment = 0.0f;
		Engine::Vector3 faceNormal{};
		if (selfShape->type == Engine::ColliderShapeType::AABB3D) {
			FindClosestTransformAxis(transform, normal, alignment, faceNormal);
		} else {
			const uint32_t axisIndex = FindClosestBoxAxis(*selfShape, normal, alignment);
			faceNormal = selfShape->axes[axisIndex];
		}
		if (alignment < kSupportAlignmentMin) {
			return;
		}
		if (Engine::Vector3::Dot(faceNormal, normal) < 0.0f) {
			faceNormal = -faceNormal;
		}

		Engine::Vector3 correctionAxis = Engine::Vector3::Cross(faceNormal, normal);
		const float correctionSin = correctionAxis.Length();
		if (correctionSin <= 0.0000001f) {
			return;
		}
		correctionAxis *= 1.0f / correctionSin;
		const float correctionCos = std::clamp(Engine::Vector3::Dot(faceNormal, normal), -1.0f, 1.0f);
		const float correctionAngle = std::atan2(correctionSin, correctionCos);
		Engine::Quaternion correction = Engine::Quaternion::MakeAxisAngle(correctionAxis, correctionAngle);

		// 追加形状回転より外側で求めた補正をTransformのローカル回転へ変換する
		if (selfShape->type != Engine::ColliderShapeType::AABB3D) {
			const auto* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
			if (!collision || !collision->shape.useTransformRotation) {
				return;
			}
			const Engine::Quaternion shapeRotation = Engine::Quaternion::FromEulerDegrees(collision->shape.rotationDegrees);
			correction = Engine::Quaternion::Inverse(shapeRotation) * correction * shapeRotation;
		}

		transform.localRotation = Engine::Quaternion::Normalize(correction * transform.localRotation);
		if (std::fabs(transform.localRotation.x) <= 0.000001f) {
			transform.localRotation.x = 0.0f;
		}
		if (std::fabs(transform.localRotation.y) <= 0.000001f) {
			transform.localRotation.y = 0.0f;
		}
		if (std::fabs(transform.localRotation.z) <= 0.000001f) {
			transform.localRotation.z = 0.0f;
		}
		if (std::fabs(transform.localRotation.w - 1.0f) <= 0.000001f) {
			transform.localRotation.w = 1.0f;
		}
		transform.localRotation = Engine::Quaternion::Normalize(transform.localRotation);
		UpdateTransformWorldMatrix(world, entity, transform);
	}

	// 重心が支持面内にあり、自身の面が支持面へ揃っているか
	bool IsCenterSupported3D(const Engine::TransformComponent& transform, const Engine::Vector3& centerOfMass,
		const Engine::Vector3& normal, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape) {

		if (normal.y < kSupportNormalMin || !IsBoxShape3D(selfShape) || !IsBoxShape3D(supportShape)) {
			return false;
		}

		float supportAlignment = 0.0f;
		const uint32_t supportAxis = FindClosestBoxAxis(*supportShape, normal, supportAlignment);
		float selfAlignment = 0.0f;
		if (selfShape->type == Engine::ColliderShapeType::AABB3D) {
			Engine::Vector3 selfAxis{};
			FindClosestTransformAxis(transform, normal, selfAlignment, selfAxis);
		} else {
			FindClosestBoxAxis(*selfShape, normal, selfAlignment);
		}
		if (supportAlignment < kSupportAlignmentMin || selfAlignment < kSupportAlignmentMin) {
			return false;
		}

		const Engine::Vector3 local = centerOfMass - supportShape->center;
		for (uint32_t i = 0; i < 3; ++i) {

			if (i == supportAxis) {
				continue;
			}
			const float halfExtent =
				i == 0 ? supportShape->halfExtents.x : (i == 1 ? supportShape->halfExtents.y : supportShape->halfExtents.z);
			if (halfExtent + kSupportEdgeTolerance < std::fabs(Engine::Vector3::Dot(local, supportShape->axes[i]))) {
				return false;
			}
		}
		return true;
	}

	// 2Dで重心が支持面内にあり、自身の辺が支持面へ揃っているか
	bool IsCenterSupported2D(const Engine::Vector3& centerOfMass, const Engine::Vector3& normal,
		const Engine::CollisionShapeInstance* selfShape, const Engine::CollisionShapeInstance* supportShape) {

		if (normal.y < kSupportNormalMin || !selfShape || !supportShape ||
			selfShape->type != Engine::ColliderShapeType::Quad2D || supportShape->type != Engine::ColliderShapeType::Quad2D) {
			return false;
		}

		float supportAlignment = 0.0f;
		const uint32_t supportAxis = FindClosestBoxAxis(*supportShape, normal, supportAlignment);
		float selfAlignment = 0.0f;
		FindClosestBoxAxis(*selfShape, normal, selfAlignment);
		if (1 < supportAxis || supportAlignment < kSupportAlignmentMin || selfAlignment < kSupportAlignmentMin) {
			return false;
		}

		const uint32_t tangentAxis = supportAxis == 0 ? 1 : 0;
		const float halfExtent = tangentAxis == 0 ? supportShape->halfExtents.x : supportShape->halfExtents.y;
		const Engine::Vector3 local = centerOfMass - supportShape->center;
		return std::fabs(Engine::Vector3::Dot(local, supportShape->axes[tangentAxis])) <= halfExtent + kSupportEdgeTolerance;
	}

	// 支持面に対する傾きだけを止め、法線まわりの回転は摩擦で減衰させる
	void StabilizeSupportedRotation(Engine::RigidbodyComponent& body, const Engine::Vector3& normal) {

		const float spin = Engine::Vector3::Dot(body.angularVelocity, normal);
		body.angularVelocity = normal * spin * std::clamp(1.0f - body.friction, 0.0f, 1.0f);
		if (body.angularVelocity.Length() <= kRestingAngularSpeed) {
			body.angularVelocity = Engine::Vector3::AnyInit(0.0f);
		}
	}

}
