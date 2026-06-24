#include "AffineDecompose.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>

//============================================================================
//	AffineDecompose methods
//============================================================================
Engine::Quaternion Engine::QuaternionFromRotationMatrixRowVector(const Matrix4x4& rowVectorMatrix) {

	// row-vector行列なので、標準的なcolumn-vector変換式を使うために転置して読む
	const float m00 = rowVectorMatrix.m[0][0];
	const float m01 = rowVectorMatrix.m[1][0];
	const float m02 = rowVectorMatrix.m[2][0];

	const float m10 = rowVectorMatrix.m[0][1];
	const float m11 = rowVectorMatrix.m[1][1];
	const float m12 = rowVectorMatrix.m[2][1];

	const float m20 = rowVectorMatrix.m[0][2];
	const float m21 = rowVectorMatrix.m[1][2];
	const float m22 = rowVectorMatrix.m[2][2];

	Quaternion q{};

	const float trace = m00 + m11 + m22;
	if (0.0f < trace) {

		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		q.w = 0.25f * s;
		q.x = (m21 - m12) / s;
		q.y = (m02 - m20) / s;
		q.z = (m10 - m01) / s;
	} else if (m00 > m11 && m00 > m22) {

		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		q.w = (m21 - m12) / s;
		q.x = 0.25f * s;
		q.y = (m01 + m10) / s;
		q.z = (m02 + m20) / s;
	} else if (m11 > m22) {

		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		q.w = (m02 - m20) / s;
		q.x = (m01 + m10) / s;
		q.y = 0.25f * s;
		q.z = (m12 + m21) / s;
	} else {

		const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
		q.w = (m10 - m01) / s;
		q.x = (m02 + m20) / s;
		q.y = (m12 + m21) / s;
		q.z = 0.25f * s;
	}
	return Quaternion::Normalize(q);
}

bool Engine::DecomposeAffine3D(const Matrix4x4& matrix, Vector3& outPos, Quaternion& outRotation, Vector3& outScale) {

	constexpr float kEps = 1e-6f;

	outPos = matrix.GetTranslationValue();

	Vector3 axisX(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]);
	Vector3 axisY(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]);
	Vector3 axisZ(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]);

	outScale.x = axisX.Length();
	outScale.y = axisY.Length();
	outScale.z = axisZ.Length();

	if (outScale.x <= kEps || outScale.y <= kEps || outScale.z <= kEps) {
		return false;
	}

	axisX /= outScale.x;
	axisY /= outScale.y;
	axisZ /= outScale.z;

	// 負スケール補正
	const float handedness = Vector3::Dot(Vector3::Cross(axisX, axisY), axisZ);
	if (handedness < 0.0f) {
		outScale.z = -outScale.z;
		axisZ = -axisZ;
	}

	Matrix4x4 rotationMatrix = Matrix4x4::Identity();
	rotationMatrix.m[0][0] = axisX.x; rotationMatrix.m[0][1] = axisX.y; rotationMatrix.m[0][2] = axisX.z;
	rotationMatrix.m[1][0] = axisY.x; rotationMatrix.m[1][1] = axisY.y; rotationMatrix.m[1][2] = axisY.z;
	rotationMatrix.m[2][0] = axisZ.x; rotationMatrix.m[2][1] = axisZ.y; rotationMatrix.m[2][2] = axisZ.z;

	outRotation = QuaternionFromRotationMatrixRowVector(rotationMatrix);
	return true;
}

Engine::Matrix4x4 Engine::BuildParentFollowMatrix(const Matrix4x4& parentWorld, bool ignoreScale, bool ignoreRotation) {

	// どちらも継承するなら親ワールドをそのまま使う
	if (!ignoreScale && !ignoreRotation) {
		return parentWorld;
	}

	const Vector3 translation = parentWorld.GetTranslationValue();
	const Vector3 axisX(parentWorld.m[0][0], parentWorld.m[0][1], parentWorld.m[0][2]);
	const Vector3 axisY(parentWorld.m[1][0], parentWorld.m[1][1], parentWorld.m[1][2]);
	const Vector3 axisZ(parentWorld.m[2][0], parentWorld.m[2][1], parentWorld.m[2][2]);
	const float scaleX = axisX.Length();
	const float scaleY = axisY.Length();
	const float scaleZ = axisZ.Length();

	// 回転無視ならワールド軸、そうでなければ正規化した軸方向を使う
	const Vector3 dirX = ignoreRotation ? Vector3(1.0f, 0.0f, 0.0f) : (scaleX > 1e-6f ? axisX / scaleX : Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 dirY = ignoreRotation ? Vector3(0.0f, 1.0f, 0.0f) : (scaleY > 1e-6f ? axisY / scaleY : Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 dirZ = ignoreRotation ? Vector3(0.0f, 0.0f, 1.0f) : (scaleZ > 1e-6f ? axisZ / scaleZ : Vector3(0.0f, 0.0f, 1.0f));
	// スケール無視なら等倍
	const float useX = ignoreScale ? 1.0f : scaleX;
	const float useY = ignoreScale ? 1.0f : scaleY;
	const float useZ = ignoreScale ? 1.0f : scaleZ;

	Matrix4x4 result = Matrix4x4::Identity();
	result.m[0][0] = dirX.x * useX; result.m[0][1] = dirX.y * useX; result.m[0][2] = dirX.z * useX;
	result.m[1][0] = dirY.x * useY; result.m[1][1] = dirY.y * useY; result.m[1][2] = dirY.z * useY;
	result.m[2][0] = dirZ.x * useZ; result.m[2][1] = dirZ.y * useZ; result.m[2][2] = dirZ.z * useZ;
	result.m[3][0] = translation.x; result.m[3][1] = translation.y; result.m[3][2] = translation.z;
	return result;
}
