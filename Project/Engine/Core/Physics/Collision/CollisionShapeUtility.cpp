#include "CollisionShapeUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CollisionShapeUtility internal
//============================================================================
namespace {

	// Vector3の各要素を絶対値にする
	Engine::Vector3 AbsVector(const Engine::Vector3& value) {

		return Engine::Vector3(std::fabs(value.x), std::fabs(value.y), std::fabs(value.z));
	}

	// 行列から指定基底方向の軸を取り出す
	Engine::Vector3 ExtractAxis(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		return Engine::Vector3::TransferNormal(basis, matrix);
	}

	// 行列から指定基底方向のスケールを取り出す
	float ExtractScale(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		const float length = ExtractAxis(matrix, basis).Length();
		return length <= 0.0001f ? 1.0f : length;
	}

	// TransformのworldMatrixからワールドスケールを取り出す
	Engine::Vector3 ExtractWorldScale(const Engine::TransformComponent& transform) {

		return Engine::Vector3(
			ExtractScale(transform.worldMatrix, Engine::Vector3(1.0f, 0.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 1.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 0.0f, 1.0f)));
	}

	// 形状のワールド中心を作成する
	Engine::Vector3 MakeWorldCenter(const Engine::CollisionShape& shape, const Engine::TransformComponent& transform) {

		return transform.worldMatrix.GetTranslationValue() +
			Engine::Vector3::TransferNormal(shape.offset, transform.worldMatrix);
	}

	// Capsule2DのオフセットはローカルXYのみを使用する
	Engine::Vector3 MakeCapsule2DWorldCenter(
		const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		const Engine::Vector3 offset(shape.offset.x, shape.offset.y, 0.0f);
		return transform.worldMatrix.GetTranslationValue() +
			Engine::Vector3::TransferNormal(offset, transform.worldMatrix);
	}

	// 形状に適用する回転行列を作成する
	Engine::Matrix4x4 MakeShapeRotationMatrix(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		Engine::Quaternion rotation = Engine::Quaternion::FromEulerDegrees(shape.rotationDegrees);
		if (shape.useTransformRotation) {
			rotation = rotation * transform.localRotation;
		}
		return Engine::Quaternion::MakeRotateMatrix(rotation);
	}

	// Capsule2Dは形状とTransformのZ回転のみを使用する
	Engine::Matrix4x4 MakeCapsule2DRotationMatrix(
		const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		float rotationZ = shape.rotationDegrees.z;
		if (shape.useTransformRotation) {
			rotationZ += Engine::Quaternion::ToEulerDegrees(
				transform.localRotation).z;
		}
		return Engine::Quaternion::MakeRotateMatrix(
			Engine::Quaternion::FromEulerDegrees(
				Engine::Vector3(0.0f, 0.0f, rotationZ)));
	}

	// カプセル軸を配列indexへ変換する、2DのZ指定はYへ戻す
	uint32_t GetCapsuleAxisIndex(const Engine::CollisionShape& shape) {

		switch (shape.capsuleAxis) {
		case Engine::CapsuleAxis::X:
			return 0;
		case Engine::CapsuleAxis::Z:
			return shape.type == Engine::ColliderShapeType::Capsule2D ? 1 : 2;
		default:
			return 1;
		}
	}

	// Vector3から指定要素を取得する
	float GetComponent(const Engine::Vector3& value, uint32_t index) {

		return index == 0 ? value.x : (index == 1 ? value.y : value.z);
	}
}

//============================================================================
//	CollisionShapeUtility classMethods
//============================================================================
Engine::CollisionShapeInstance Engine::CollisionShapeUtility::BuildShapeInstance(const Entity& entity,
	const CollisionShape& shape, uint32_t shapeIndex, const TransformComponent& transform) {

	CollisionShapeInstance instance{};
	instance.entity = entity;
	instance.shapeIndex = shapeIndex;
	instance.type = shape.type;
	instance.trigger = shape.isTrigger;
	instance.center = shape.type == ColliderShapeType::Capsule2D ?
		MakeCapsule2DWorldCenter(shape, transform) :
		MakeWorldCenter(shape, transform);
	instance.segmentStart = instance.center;
	instance.segmentEnd = instance.center;

	// Transformのスケールを衝突サイズに反映する
	const Vector3 scale = AbsVector(ExtractWorldScale(transform));
	instance.radius = shape.radius * (std::max)({ scale.x, scale.y, scale.z });
	instance.halfExtents = Vector3(
		shape.halfExtents3D.x * scale.x,
		shape.halfExtents3D.y * scale.y,
		shape.halfExtents3D.z * scale.z);

	if (shape.type == ColliderShapeType::Circle2D) {
		instance.radius = shape.radius * (std::max)(scale.x, scale.y);
	}
	if (shape.type == ColliderShapeType::Quad2D) {
		instance.halfExtents = Vector3(shape.halfSize2D.x * scale.x, shape.halfSize2D.y * scale.y, 0.0f);
	}

	// 回転を使わない形状はワールド軸に固定する
	const bool rotate2D =
		(shape.type == ColliderShapeType::Quad2D && shape.rotatedQuad) ||
		shape.type == ColliderShapeType::Capsule2D;
	const bool rotate3D = shape.type == ColliderShapeType::OBB3D ||
		shape.type == ColliderShapeType::Capsule3D;
	if (rotate2D || rotate3D) {

		const Matrix4x4 rotation = shape.type == ColliderShapeType::Capsule2D ?
			MakeCapsule2DRotationMatrix(shape, transform) :
			MakeShapeRotationMatrix(shape, transform);
		instance.axes[0] = Vector3::NormalizeOr(
			Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotation),
			Vector3(1.0f, 0.0f, 0.0f));
		instance.axes[1] = Vector3::NormalizeOr(
			Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotation),
			Vector3(0.0f, 1.0f, 0.0f));
		instance.axes[2] = Vector3::NormalizeOr(
			Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotation),
			Vector3(0.0f, 0.0f, 1.0f));
	} else {

		instance.axes[0] = Vector3(1.0f, 0.0f, 0.0f);
		instance.axes[1] = Vector3(0.0f, 1.0f, 0.0f);
		instance.axes[2] = Vector3(0.0f, 0.0f, 1.0f);
	}

	if (shape.type == ColliderShapeType::Capsule2D) {

		const uint32_t axisIndex = GetCapsuleAxisIndex(shape);
		const Vector2 size(
			(std::max)(shape.capsuleSize2D.x * scale.x, 0.0f),
			(std::max)(shape.capsuleSize2D.y * scale.y, 0.0f));
		const float axisSize = axisIndex == 0 ? size.x : size.y;
		const float perpendicularSize = axisIndex == 0 ? size.y : size.x;
		instance.radius = (std::min)(axisSize, perpendicularSize) * 0.5f;
		const float segmentHalfLength =
			(std::max)(axisSize * 0.5f - instance.radius, 0.0f);
		const Vector3 axis = instance.axes[axisIndex];
		instance.segmentStart = instance.center - axis * segmentHalfLength;
		instance.segmentEnd = instance.center + axis * segmentHalfLength;
	}
	if (shape.type == ColliderShapeType::Capsule3D) {

		const uint32_t axisIndex = GetCapsuleAxisIndex(shape);
		float radiusScale = 0.0f;
		for (uint32_t i = 0; i < 3; ++i) {
			if (i != axisIndex) {
				radiusScale = (std::max)(radiusScale, GetComponent(scale, i));
			}
		}
		instance.radius = (std::max)(shape.radius, 0.0f) * radiusScale;
		const float halfHeight = (std::max)(
			(std::max)(shape.capsuleHeight, 0.0f) * GetComponent(scale, axisIndex) * 0.5f,
			instance.radius);
		const float segmentHalfLength = halfHeight - instance.radius;
		const Vector3 axis = instance.axes[axisIndex];
		instance.segmentStart = instance.center - axis * segmentHalfLength;
		instance.segmentEnd = instance.center + axis * segmentHalfLength;
	}
	return instance;
}
