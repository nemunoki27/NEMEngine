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

	// 形状に適用する回転行列を作成する
	Engine::Matrix4x4 MakeShapeRotationMatrix(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		Engine::Quaternion rotation = Engine::Quaternion::FromEulerDegrees(shape.rotationDegrees);
		if (shape.useTransformRotation) {
			rotation = rotation * transform.localRotation;
		}
		return Engine::Quaternion::MakeRotateMatrix(rotation);
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
	instance.center = MakeWorldCenter(shape, transform);

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
	const bool rotate2D = shape.type == ColliderShapeType::Quad2D && shape.rotatedQuad;
	const bool rotate3D = shape.type == ColliderShapeType::OBB3D;
	if (rotate2D || rotate3D) {

		const Matrix4x4 rotation = MakeShapeRotationMatrix(shape, transform);
		instance.axes[0] = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotation), Vector3(1.0f, 0.0f, 0.0f));
		instance.axes[1] = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotation), Vector3(0.0f, 1.0f, 0.0f));
		instance.axes[2] = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotation), Vector3(0.0f, 0.0f, 1.0f));
	} else {

		instance.axes[0] = Vector3(1.0f, 0.0f, 0.0f);
		instance.axes[1] = Vector3(0.0f, 1.0f, 0.0f);
		instance.axes[2] = Vector3(0.0f, 0.0f, 1.0f);
	}
	return instance;
}
