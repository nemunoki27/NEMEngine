#include "Matrix4x4.h"

using namespace SakuEngine;

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/MathLib/Quaternion.h>

//============================================================================*/
//	Matrix4x4 classMethods
//============================================================================*/

Matrix4x4 Matrix4x4::operator+(const Matrix4x4& other) const {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = this->m[i][j] + other.m[i][j];
		}
	}
	return result;
}

Matrix4x4 Matrix4x4::operator-(const Matrix4x4& other) const {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = this->m[i][j] - other.m[i][j];
		}
	}
	return result;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const {
	return Multiply(*this, other);
}

Matrix4x4 Matrix4x4::operator/(float scalar) const {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = this->m[i][j] / scalar;
		}
	}
	return result;
}

Matrix4x4& Matrix4x4::operator+=(const Matrix4x4& other) {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			this->m[i][j] += other.m[i][j];
		}
	}
	return *this;
}

Matrix4x4& Matrix4x4::operator-=(const Matrix4x4& other) {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			this->m[i][j] -= other.m[i][j];
		}
	}
	return *this;
}

Matrix4x4& Matrix4x4::operator*=(const Matrix4x4& other) {

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			this->m[i][j] *= other.m[i][j];
		}
	}
	return *this;
}

Matrix4x4& Matrix4x4::operator/=(const Matrix4x4& other) {

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			this->m[i][j] /= other.m[i][j];
		}
	}
	return *this;
}

Matrix4x4& Matrix4x4::operator=(const Matrix4x4& other) {
	if (this != &other) {
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				m[i][j] = other.m[i][j];
			}
		}
	}
	return *this;
}

bool Matrix4x4::operator==(const Matrix4x4& other) const {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			if (m[i][j] != other.m[i][j]) {
				return false;
			}
		}
	}
	return true;
}

Matrix4x4 Matrix4x4::Zero() {
	return Matrix4x4{ 0.0f, 0.0f, 0.0f, 0.0f,
						0.0f, 0.0f, 0.0f, 0.0f,
						0.0f, 0.0f,0.0f, 0.0f,
						0.0f, 0.0f, 0.0f, 0.0f };
}

Matrix4x4 Matrix4x4::Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {

	Matrix4x4 matrix{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0;
			for (int k = 0; k < 4; ++k) {
				matrix.m[i][j] += m1.m[i][k] * m2.m[k][j];
			}
		}
	}
	return matrix;
}

Matrix4x4 Matrix4x4::Inverse(const Matrix4x4& m) {

	Matrix4x4 matrix = {};

	float det =
		m.m[0][0] * (m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] +
			m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][3] * m.m[3][2] -
			m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[1][3] * m.m[2][2] * m.m[3][1]) -
		m.m[0][1] * (m.m[1][0] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][0] +
			m.m[1][3] * m.m[2][0] * m.m[3][2] - m.m[1][0] * m.m[2][3] * m.m[3][2] -
			m.m[1][2] * m.m[2][0] * m.m[3][3] - m.m[1][3] * m.m[2][2] * m.m[3][0]) +
		m.m[0][2] * (m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] +
			m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][0] * m.m[2][3] * m.m[3][1] -
			m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[1][3] * m.m[2][1] * m.m[3][0]) -
		m.m[0][3] * (m.m[1][0] * m.m[2][1] * m.m[3][2] + m.m[1][1] * m.m[2][2] * m.m[3][0] +
			m.m[1][2] * m.m[2][0] * m.m[3][1] - m.m[1][0] * m.m[2][2] * m.m[3][1] -
			m.m[1][1] * m.m[2][0] * m.m[3][2] - m.m[1][2] * m.m[2][1] * m.m[3][0]);

	float invDet = 1.0f / det;

	matrix.m[0][0] = (m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] +
		m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][3] * m.m[3][2] -
		m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[1][3] * m.m[2][2] * m.m[3][1]) *
		invDet;
	matrix.m[0][1] = (m.m[0][1] * m.m[2][3] * m.m[3][2] + m.m[0][2] * m.m[2][1] * m.m[3][3] +
		m.m[0][3] * m.m[2][2] * m.m[3][1] - m.m[0][1] * m.m[2][2] * m.m[3][3] -
		m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2]) *
		invDet;
	matrix.m[0][2] = (m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] +
		m.m[0][3] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][3] * m.m[3][2] -
		m.m[0][2] * m.m[1][1] * m.m[3][3] - m.m[0][3] * m.m[1][2] * m.m[3][1]) *
		invDet;
	matrix.m[0][3] = (m.m[0][1] * m.m[1][3] * m.m[2][2] + m.m[0][2] * m.m[1][1] * m.m[2][3] +
		m.m[0][3] * m.m[1][2] * m.m[2][1] - m.m[0][1] * m.m[1][2] * m.m[2][3] -
		m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2]) *
		invDet;

	matrix.m[1][0] = (m.m[1][0] * m.m[2][3] * m.m[3][2] + m.m[1][2] * m.m[2][0] * m.m[3][3] +
		m.m[1][3] * m.m[2][2] * m.m[3][0] - m.m[1][0] * m.m[2][2] * m.m[3][3] -
		m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[1][3] * m.m[2][0] * m.m[3][2]) *
		invDet;
	matrix.m[1][1] = (m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] +
		m.m[0][3] * m.m[2][0] * m.m[3][2] - m.m[0][0] * m.m[2][3] * m.m[3][2] -
		m.m[0][2] * m.m[2][0] * m.m[3][3] - m.m[0][3] * m.m[2][2] * m.m[3][0]) *
		invDet;
	matrix.m[1][2] = (m.m[0][0] * m.m[1][3] * m.m[3][2] + m.m[0][2] * m.m[1][0] * m.m[3][3] +
		m.m[0][3] * m.m[1][2] * m.m[3][0] - m.m[0][0] * m.m[1][2] * m.m[3][3] -
		m.m[0][2] * m.m[1][3] * m.m[3][0] - m.m[0][3] * m.m[1][0] * m.m[3][2]) *
		invDet;
	matrix.m[1][3] = (m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] +
		m.m[0][3] * m.m[1][0] * m.m[2][2] - m.m[0][0] * m.m[1][3] * m.m[2][2] -
		m.m[0][2] * m.m[1][0] * m.m[2][3] - m.m[0][3] * m.m[1][2] * m.m[2][0]) *
		invDet;

	matrix.m[2][0] = (m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] +
		m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][0] * m.m[2][3] * m.m[3][1] -
		m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[1][3] * m.m[2][1] * m.m[3][0]) *
		invDet;
	matrix.m[2][1] = (m.m[0][0] * m.m[2][3] * m.m[3][1] + m.m[0][1] * m.m[2][0] * m.m[3][3] +
		m.m[0][3] * m.m[2][1] * m.m[3][0] - m.m[0][0] * m.m[2][1] * m.m[3][3] -
		m.m[0][1] * m.m[2][3] * m.m[3][0] - m.m[0][3] * m.m[2][0] * m.m[3][1]) *
		invDet;
	matrix.m[2][2] = (m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] +
		m.m[0][3] * m.m[1][0] * m.m[3][1] - m.m[0][0] * m.m[1][3] * m.m[3][1] -
		m.m[0][1] * m.m[1][0] * m.m[3][3] - m.m[0][3] * m.m[1][1] * m.m[3][0]) *
		invDet;
	matrix.m[2][3] = (m.m[0][0] * m.m[1][3] * m.m[2][1] + m.m[0][1] * m.m[1][0] * m.m[2][3] +
		m.m[0][3] * m.m[1][1] * m.m[2][0] - m.m[0][0] * m.m[1][1] * m.m[2][3] -
		m.m[0][1] * m.m[1][3] * m.m[2][0] - m.m[0][3] * m.m[1][0] * m.m[2][1]) *
		invDet;

	matrix.m[3][0] = (m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[1][1] * m.m[2][0] * m.m[3][2] +
		m.m[1][2] * m.m[2][1] * m.m[3][0] - m.m[1][0] * m.m[2][1] * m.m[3][2] -
		m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1]) *
		invDet;
	matrix.m[3][1] = (m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] +
		m.m[0][2] * m.m[2][0] * m.m[3][1] - m.m[0][0] * m.m[2][2] * m.m[3][1] -
		m.m[0][1] * m.m[2][0] * m.m[3][2] - m.m[0][2] * m.m[2][1] * m.m[3][0]) *
		invDet;
	matrix.m[3][2] = (m.m[0][0] * m.m[1][2] * m.m[3][1] + m.m[0][1] * m.m[1][0] * m.m[3][2] +
		m.m[0][2] * m.m[1][1] * m.m[3][0] - m.m[0][0] * m.m[1][1] * m.m[3][2] -
		m.m[0][1] * m.m[1][2] * m.m[3][0] - m.m[0][2] * m.m[1][0] * m.m[3][1]) *
		invDet;
	matrix.m[3][3] = (m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] +
		m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][0] * m.m[1][2] * m.m[2][1] -
		m.m[0][1] * m.m[1][0] * m.m[2][2] - m.m[0][2] * m.m[1][1] * m.m[2][0]) *
		invDet;

	if (det == 0) {

		return matrix;
	}

	return matrix;
}

Matrix4x4 Matrix4x4::Transpose(const Matrix4x4& m) {

	Matrix4x4 matrix;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = m.m[j][i];
		}
	}

	return matrix;
}

Matrix4x4 Matrix4x4::MakeIdentity4x4() {

	Matrix4x4 matrix{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	return matrix;
}

Matrix4x4 Matrix4x4::MakeScaleMatrix(const SakuEngine::Vector3& scale) {

	Matrix4x4 scaleMatrix = {
		scale.x, 0.0f, 0.0f ,0.0f,
		0.0f, scale.y, 0.0f, 0.0f,
		0.0f, 0.0f, scale.z, 0.0f,
		0.0f ,0.0f, 0.0f, 1.0f
	};

	return scaleMatrix;
}

Matrix4x4 Matrix4x4::MakePitchMatrix(float radian) {

	float cosTheta = std::cos(radian);
	float sinTheta = std::sin(radian);

	Matrix4x4 pitchMatrix = {
		1.0f, 0.0f,0.0f,0.0f,
		0.0f, cosTheta, sinTheta, 0.0f,
		0.0f, -sinTheta, cosTheta, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f };

	return pitchMatrix;
}

Matrix4x4 Matrix4x4::MakeYawMatrix(float radian) {

	float cosTheta = std::cos(radian);
	float sinTheta = std::sin(radian);

	Matrix4x4 yawMatrix = {
		cosTheta, 0.0f, -sinTheta, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		sinTheta, 0.0f, cosTheta, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	return yawMatrix;
}

Matrix4x4 Matrix4x4::MakeRollMatrix(float radian) {

	float cosTheta = std::cos(radian);
	float sinTheta = std::sin(radian);

	Matrix4x4 rollMatrix = {
		cosTheta, sinTheta, 0.0f, 0.0f,
		-sinTheta, cosTheta, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f,0.0f,0.0f, 1.0f
	};

	return rollMatrix;
}

Matrix4x4 SakuEngine::Matrix4x4::MakeRotateMatrix(const SakuEngine::Vector3& rotate) {

	Matrix4x4 pitchMatrix = MakePitchMatrix(rotate.x);	// X軸回転行列
	Matrix4x4 yawMatrix = MakeYawMatrix(rotate.y);		// Y軸回転行列
	Matrix4x4 rollMatrix = MakeRollMatrix(rotate.z);	// Z軸回転行列

	Matrix4x4 rotateMatrix = Multiply(pitchMatrix, Multiply(yawMatrix, rollMatrix));

	return rotateMatrix;
}

Matrix4x4 Matrix4x4::MakeTranslateMatrix(const SakuEngine::Vector3& translate) {

	Matrix4x4 translateMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		translate.x, translate.y, translate.z, 1.0f
	};

	return translateMatrix;
}

Matrix4x4 Matrix4x4::MakeAffineMatrix(const SakuEngine::Vector3& scale, const SakuEngine::Vector3& rotate, const SakuEngine::Vector3& translate) {

	Matrix4x4 matrix = {};

	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateMatrix = MakeRotateMatrix(rotate);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	matrix = Multiply(scaleMatrix, rotateMatrix);
	matrix = Multiply(matrix, translateMatrix);

	return matrix;
}

Matrix4x4 Matrix4x4::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {

	Matrix4x4 matrix = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0.0f;
		}
	}

	matrix.m[0][0] = 2.0f / (right - left);
	matrix.m[1][1] = 2.0f / (top - bottom);
	matrix.m[2][2] = 1.0f / (farClip - nearClip);
	matrix.m[3][0] = (left + right) / (left - right);
	matrix.m[3][1] = (top + bottom) / (bottom - top);
	matrix.m[3][2] = nearClip / (nearClip - farClip);
	matrix.m[3][3] = 1.0f;

	return matrix;
}

Matrix4x4 Matrix4x4::MakeShadowOrthographicMatrix(float width, float height, float nearClip, float farClip) {

	Matrix4x4 matrix = {};

	float left = -width * 0.5f;
	float right = width * 0.5f;
	float bottom = -height * 0.5f;
	float top = height * 0.5f;

	// 行列要素をゼロ初期化
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0.0f;
		}
	}

	matrix.m[0][0] = 2.0f / (right - left);
	matrix.m[1][1] = 2.0f / (top - bottom);
	matrix.m[2][2] = 1.0f / (farClip - nearClip);
	matrix.m[3][0] = (left + right) / (left - right);
	matrix.m[3][1] = (top + bottom) / (bottom - top);
	matrix.m[3][2] = nearClip / (nearClip - farClip);
	matrix.m[3][3] = 1.0f;

	return matrix;
}

Matrix4x4 Matrix4x4::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {

	Matrix4x4 matrix = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0.0f;
		}
	}

	matrix.m[0][0] = 1.0f / (aspectRatio * std::tanf(fovY / 2.0f));
	matrix.m[1][1] = 1.0f / std::tanf(fovY / 2.0f);
	matrix.m[2][2] = farClip / (farClip - nearClip);
	matrix.m[2][3] = 1.0f;
	matrix.m[3][2] = (-farClip * nearClip) / (farClip - nearClip);

	return matrix;
}

Matrix4x4 Matrix4x4::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {

	Matrix4x4 matrix = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0.0f;
		}
	}

	matrix.m[0][0] = width / 2.0f;
	matrix.m[1][1] = -height / 2.0f;
	matrix.m[2][2] = maxDepth - minDepth;
	matrix.m[3][0] = left + width / 2.0f;
	matrix.m[3][1] = top + height / 2.0f;
	matrix.m[3][2] = minDepth;
	matrix.m[3][3] = 1.0f;

	return matrix;
}

// 任意軸アフィン変換
Matrix4x4 Matrix4x4::MakeAxisAffineMatrix(const SakuEngine::Vector3& scale, const Quaternion& rotate, const SakuEngine::Vector3& translate) {

	Matrix4x4 matrix = {};

	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateMatrix = Quaternion::MakeRotateMatrix(rotate);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	matrix = Multiply(scaleMatrix, rotateMatrix);
	matrix = Multiply(matrix, translateMatrix);

	return matrix;
}

Vector3 Matrix4x4::TransformPoint(const SakuEngine::Vector3& point) const {

	// 4次元ベクトル (x, y, z, w) を作成
	float x = point.x * m[0][0] + point.y * m[0][1] + point.z * m[0][2] + m[0][3];
	float y = point.x * m[1][0] + point.y * m[1][1] + point.z * m[1][2] + m[1][3];
	float z = point.x * m[2][0] + point.y * m[2][1] + point.z * m[2][2] + m[2][3];
	float w = point.x * m[3][0] + point.y * m[3][1] + point.z * m[3][2] + m[3][3];

	// wで正規化して3D座標を返す
	if (w != 0.0f) {
		x /= w;
		y /= w;
		z /= w;
	}

	return Vector3(x, y, z);
}

Vector3 Matrix4x4::GetTranslationValue() const {

	return Vector3(m[3][0], m[3][1], m[3][2]);
}

Vector3 Matrix4x4::GetRotationValue() const {

	Vector3 euler{};
	float sy = m[2][0];
	if (std::abs(sy) < 0.999999f) {

		euler.y = std::asin(sy);                 // yaw
		euler.x = std::atan2(-m[2][1], m[2][2]); // pitch
		euler.z = std::atan2(-m[1][0], m[0][0]); // roll
	} else {

		euler.y = (sy > 0.0f) ? +std::numbers::pi_v<float> *0.5f
			: -std::numbers::pi_v<float> *0.5f;

		// pitchとrollはまとめて求める
		euler.x = std::atan2(m[0][1], m[1][1]);
		euler.z = 0.0f;
	}
	return euler;
}
