#include "MathUtils.h"

using namespace SakuEngine;

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Random/RandomGenerator.h>
#include <Engine/Scene/Camera/BaseCamera.h>
#include <Engine/Object/Base/GameObject3D.h>
#include <Engine/Config.h>

//============================================================================
//	MathUtils namespaceMethods
//============================================================================

float Math::AbsFloat(float v) {

	return v < 0.0f ? -v : v;
}

float Math::GetYawRadian(const Vector3& direction) {

	return std::atan2(direction.z, direction.x);
}

float Math::WrapDegree(float value) {

	while (value < 0.0f) {

		value += 360.0f;
	}
	while (value >= 360.0f) {

		value -= 360.0f;
	}
	return value;
}

float Math::WrapPi(float value) {

	while (value > pi) {

		value -= 2.0f * pi;
	}
	while (value < -pi) {

		value += 2.0f * pi;
	}
	return value;
}

RECT Math::MakeClientRect(const Vector2& size, const Vector2& pos) {

	const float halfX = size.x * 0.5f;
	const float halfY = size.y * 0.5f;

	const LONG left = static_cast<LONG>(std::floor(pos.x - halfX));
	const LONG top = static_cast<LONG>(std::floor(pos.y - halfY));
	const LONG right = static_cast<LONG>(std::ceil(pos.x + halfX));
	const LONG bottom = static_cast<LONG>(std::ceil(pos.y + halfY));

	RECT rect{ left, top, right, bottom };
	return rect;
}

int Math::YawShortestDirection(const Quaternion& from, const Quaternion& to) {

	// Δ回転
	Quaternion qFrom = Quaternion::Normalize(from);
	Quaternion qTo = Quaternion::Normalize(to);
	Quaternion delta = Quaternion::Normalize(Quaternion::Multiply(qTo, Quaternion::Inverse(qFrom)));

	// Y軸まわりの回転のみ取得
	Quaternion twistY{ 0.0f, delta.y, 0.0f, delta.w };
	float len = std::sqrt(twistY.y * twistY.y + twistY.w * twistY.w);
	if (len <= Config::kEpsilon) {
		return 0;
	}
	twistY.y /= len;
	twistY.w /= len;

	// 符号付き角度
	float angle = AngleFromTwist(twistY, Axis::Y);
	angle = WrapPi(angle);

	if (std::abs(angle) <= Config::kEpsilon) {
		return 0;
	}
	return (0.0f < angle) ? +1 : -1;
}

float Math::YawSignedDelta(const Quaternion& from, const Quaternion& to) {

	Quaternion qFrom = Quaternion::Normalize(from);
	Quaternion qTo = Quaternion::Normalize(to);
	Quaternion delta = Quaternion::Normalize(Quaternion::Multiply(qTo, Quaternion::Inverse(qFrom)));

	Quaternion twistY{ 0.0f, delta.y, 0.0f, delta.w };
	float len = std::sqrt(twistY.y * twistY.y + twistY.w * twistY.w);
	if (len <= Config::kEpsilon) {
		return 0.0f;
	}
	twistY.y /= len;
	twistY.w /= len;

	float angle = AngleFromTwist(twistY, Axis::Y);
	return WrapPi(angle);
}

AnchorToDirection2D Math::YawSideFromPos(const Vector3& fromPos, const Quaternion& fromRot, const Vector3& toPos) {

	// 自分から相手への向き相手
	Vector3 to = toPos - fromPos;
	to.y = 0.0f;
	to = to.Normalize();
	// 自分の右方向ベクトル
	Quaternion fromRotation = Quaternion::Normalize(fromRot);
	Vector3 right = Quaternion::RotateVector(Direction::Get(Direction3D::Right), fromRotation);
	right.y = 0.0f;
	right = right.Normalize();

	// 右方向との内積で左右判定
	float side = Vector3::Dot(right, to);
	return (side > 0.0f) ? AnchorToDirection2D::Right : AnchorToDirection2D::Left;
}

float Math::AngleFromTwist(const Quaternion& twist, Axis axis) {

	float angle = 0.0f;
	switch (axis) {
	case Math::Axis::X:

		angle = 2.0f * std::atan2(twist.x, twist.w);
		break;
	case Math::Axis::Y:

		angle = 2.0f * std::atan2(twist.y, twist.w);
		break;
	case Math::Axis::Z:

		angle = 2.0f * std::atan2(twist.z, twist.w);
		break;
	}
	return angle;
}

Vector3 Math::RandomPointOnArc(const Vector3& center,
	const Vector3& direction, float radius, float halfAngle) {

	const float baseYaw = GetYawRadian(direction.Normalize());
	const float halfRad = pi * halfAngle / 180.0f;
	const float randYaw = RandomGenerator::Generate(-halfRad, halfRad);
	const float yaw = baseYaw + randYaw;

	return { center.x + radius * std::cos(yaw),center.y,center.z + radius * std::sin(yaw) };
}

Vector3 Math::RandomPointOnArcInSquare(const Vector3& arcCenter, const Vector3& direction,
	float radius, float halfAngle, const Vector3& squareCenter,
	float clampHalfSize, int tryCount) {

	const float baseYaw = GetYawRadian(direction.Normalize());
	const float halfRad = pi * halfAngle / 180.0f;

	// 正方形の境界
	const float minX = squareCenter.x - clampHalfSize;
	const float maxX = squareCenter.x + clampHalfSize;
	const float minZ = squareCenter.z - clampHalfSize;
	const float maxZ = squareCenter.z + clampHalfSize;

	for (int i = 0; i < tryCount; ++i) {

		const float randYaw = RandomGenerator::Generate(-halfRad, halfRad);
		const float yaw = baseYaw + randYaw;

		Vector3 dir{ std::cos(yaw), 0.0f, std::sin(yaw) };

		// その方向でx,z境界に当たるまでの距離
		const float tX = std::abs(dir.x) < 1e-6f ?
			FLT_MAX : (dir.x > 0 ? (maxX - arcCenter.x) / dir.x
				: (minX - arcCenter.x) / dir.x);
		const float tZ = std::abs(dir.z) < 1e-6f ?
			FLT_MAX : (dir.z > 0 ? (maxZ - arcCenter.z) / dir.z :
				(minZ - arcCenter.z) / dir.z);

		// 納まる座標があれば設定
		const float rMaxDir = (std::max)(0.0f, (std::min)(tX, tZ));
		if (radius <= rMaxDir) {

			return arcCenter + dir * radius;
		}
	}

	// 見つからない場合、一番近い距離にする
	return {
		std::clamp(arcCenter.x, minX, maxX),
		arcCenter.y,
		std::clamp(arcCenter.z, minZ, maxZ) };
}

Vector3 Math::RotateY(const Vector3& v, float rad) {

	Matrix4x4 rotate = Matrix4x4::MakeYawMatrix(rad);
	return Vector3::Transform(v, rotate).Normalize();
}

float Math::QuaternionAngleDeg(const Quaternion& rotateA, const Quaternion& rotateB) {

	// クォータニオンの内積を計算
	float dot = Quaternion::Dot(rotateA, rotateB);
	if (dot < 0.0f) {
		dot = -dot;
	}
	if (dot > 1.0f) {
		dot = 1.0f;
	}
	float angleRad = 2.0f * std::acos(dot);
	return angleRad * 180.0f / pi;
}

void Math::ToColumnMajor(const Matrix4x4& matrix, float out[16]) {

	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {

			out[c * 4 + r] = matrix.m[r][c];
		}
	}
}

void Math::FromColumnMajor(const float in[16], Matrix4x4& matrix) {

	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {

			matrix.m[r][c] = in[c * 4 + r];
		}
	}
}

void Math::ToFloatMatrix(const Matrix4x4& matrix, float out[16]) {

	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {

			out[r * 4 + c] = matrix.m[r][c];
		}
	}
}

Vector2 Math::ProjectToScreen(const Vector3& translation, const BaseCamera& camera) {

	Matrix4x4 viewMatrix = camera.GetViewMatrix();
	Matrix4x4 projectionMatrix = camera.GetProjectionMatrix();

	Vector3 viewPos = Vector3::Transform(translation, viewMatrix);
	Vector3 clipPos = Vector3::Transform(viewPos, projectionMatrix);

	float screenX = (clipPos.x * 0.5f + 0.5f) * Config::kWindowWidthf;
	float screenY = (1.0f - (clipPos.y * 0.5f + 0.5f)) * Config::kWindowHeightf;

	return Vector2(screenX, screenY);
}

Vector3 Math::GetFlattenPos3D(const GameObject3D& object, bool isWorld, float posY) {

	Vector3 translation = isWorld ?
		object.GetTransform().GetWorldPos() : object.GetTranslation();
	// Y座標を固定
	translation.y = posY;
	return translation;
}

float Math::GetDistance3D(const GameObject3D& object0, const GameObject3D& object1,
	bool isWorld, bool isFlattenPos) {

	// 座標取得
	Vector3 pos0 = isWorld ? object0.GetTransform().GetWorldPos() : object0.GetTranslation();
	Vector3 pos1 = isWorld ? object1.GetTransform().GetWorldPos() : object1.GetTranslation();
	if (isFlattenPos) {
		pos0.y = 0.0f;
		pos1.y = 0.0f;
	}

	// 距離を取得
	float distance = Vector3::Length(pos1 - pos0);
	return distance;
}

Vector3 Math::GetDirection3D(const GameObject3D& from, const GameObject3D& to,
	bool isWorld, const std::vector<Axis>& ignoreAxis) {

	// 座標取得
	Vector3 fromPos = isWorld ? from.GetTransform().GetWorldPos() : from.GetTranslation();
	Vector3 toPos = isWorld ? to.GetTransform().GetWorldPos() : to.GetTranslation();

	// 方向ベクトルを取得
	Vector3 direction = Vector3(toPos - fromPos).Normalize();

	// 指定軸を無視
	for (const Axis& axis : ignoreAxis) {
		switch (axis) {
		case Axis::X:
			direction.x = 0.0f;
			break;
		case Axis::Y:
			direction.y = 0.0f;
			break;
		case Axis::Z:
			direction.z = 0.0f;
			break;
		}
	}
	// 正規化して返す
	direction = direction.Normalize();
	return direction;
}

void Math::RotateToDirection3D(GameObject3D& object, const Vector3& direction, float lerpRate) {

	// 処理できない向きは早期リターン
	if (direction.Length() <= Config::kEpsilon) {
		return;
	}

	// 向きを計算
	Quaternion targetRotation = Quaternion::LookRotation(direction, Direction::Get(Direction3D::Up));
	Quaternion rotation = object.GetRotation();

	// 補間
	rotation = Quaternion::Slerp(rotation, targetRotation, lerpRate);
	object.SetRotation(rotation);
}

void Math::LookTarget3D(GameObject3D& object, const Vector3& targetPos, float lerpRate, bool useScaledDeltaTime) {

	// デルタタイム取得
	float deltaTime = useScaledDeltaTime ?
		GameTimer::GetScaledDeltaTime() : GameTimer::GetDeltaTime();

	// 前方ベクトルを取得
	Vector3 bossPos = object.GetTranslation();
	bossPos.y = 0.0f;

	// 回転を計算して設定
	Quaternion bossRotation = Quaternion::LookTarget(bossPos, targetPos,
		Direction::Get(Direction3D::Up), object.GetRotation(), lerpRate * deltaTime);
	object.SetRotation(bossRotation);
}