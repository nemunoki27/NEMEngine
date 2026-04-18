#include "Matrix4x4.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

//============================================================================
//	Matrix4x4 structMethods
//============================================================================

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
	Matrix4x4 matrix{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0;
			for (int k = 0; k < 4; ++k) {
				matrix.m[i][j] += this->m[i][k] * other.m[k][j];
			}
		}
	}
	return matrix;
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
bool Matrix4x4::operator!=(const Matrix4x4& other) const {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			if (m[i][j] == other.m[i][j]) {
				return false;
			}
		}
	}
	return true;
}

void Matrix4x4::Init() {

	*this = Identity();
}

Matrix4x4 Matrix4x4::Identity() {
	Matrix4x4 matrix{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return matrix;
}

Matrix4x4 Matrix4x4::Zero() {
	Matrix4x4 matrix{};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			matrix.m[i][j] = 0.0f;
		}
	}
	return matrix;
}

Matrix4x4 Matrix4x4::Inverse(const Matrix4x4& m) {
	Matrix4x4 matrix = {};
	float det = m.m[0][0] * (m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] +
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
		m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[1][3] * m.m[2][2] * m.m[3][1]) * invDet;
	matrix.m[0][1] = (m.m[0][1] * m.m[2][3] * m.m[3][2] + m.m[0][2] * m.m[2][1] * m.m[3][3] +
		m.m[0][3] * m.m[2][2] * m.m[3][1] - m.m[0][1] * m.m[2][2] * m.m[3][3] -
		m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2]) * invDet;
	matrix.m[0][2] = (m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] +
		m.m[0][3] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][3] * m.m[3][2] -
		m.m[0][2] * m.m[1][1] * m.m[3][3] - m.m[0][3] * m.m[1][2] * m.m[3][1]) * invDet;
	matrix.m[0][3] = (m.m[0][1] * m.m[1][3] * m.m[2][2] + m.m[0][2] * m.m[1][1] * m.m[2][3] +
		m.m[0][3] * m.m[1][2] * m.m[2][1] - m.m[0][1] * m.m[1][2] * m.m[2][3] -
		m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2]) * invDet;
	matrix.m[1][0] = (m.m[1][0] * m.m[2][3] * m.m[3][2] + m.m[1][2] * m.m[2][0] * m.m[3][3] +
		m.m[1][3] * m.m[2][2] * m.m[3][0] - m.m[1][0] * m.m[2][2] * m.m[3][3] -
		m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[1][3] * m.m[2][0] * m.m[3][2]) * invDet;
	matrix.m[1][1] = (m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] +
		m.m[0][3] * m.m[2][0] * m.m[3][2] - m.m[0][0] * m.m[2][3] * m.m[3][2] -
		m.m[0][2] * m.m[2][0] * m.m[3][3] - m.m[0][3] * m.m[2][2] * m.m[3][0]) * invDet;
	matrix.m[1][2] = (m.m[0][0] * m.m[1][3] * m.m[3][2] + m.m[0][2] * m.m[1][0] * m.m[3][3] +
		m.m[0][3] * m.m[1][2] * m.m[3][0] - m.m[0][0] * m.m[1][2] * m.m[3][3] -
		m.m[0][2] * m.m[1][3] * m.m[3][0] - m.m[0][3] * m.m[1][0] * m.m[3][2]) * invDet;
	matrix.m[1][3] = (m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] +
		m.m[0][3] * m.m[1][0] * m.m[2][2] - m.m[0][0] * m.m[1][3] * m.m[2][2] -
		m.m[0][2] * m.m[1][0] * m.m[2][3] - m.m[0][3] * m.m[1][2] * m.m[2][0]) * invDet;
	matrix.m[2][0] = (m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] +
		m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][0] * m.m[2][3] * m.m[3][1] -
		m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[1][3] * m.m[2][1] * m.m[3][0]) * invDet;
	matrix.m[2][1] = (m.m[0][0] * m.m[2][3] * m.m[3][1] + m.m[0][1] * m.m[2][0] * m.m[3][3] +
		m.m[0][3] * m.m[2][1] * m.m[3][0] - m.m[0][0] * m.m[2][1] * m.m[3][3] -
		m.m[0][1] * m.m[2][3] * m.m[3][0] - m.m[0][3] * m.m[2][0] * m.m[3][1]) * invDet;
	matrix.m[2][2] = (m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] +
		m.m[0][3] * m.m[1][0] * m.m[3][1] - m.m[0][0] * m.m[1][3] * m.m[3][1] -
		m.m[0][1] * m.m[1][0] * m.m[3][3] - m.m[0][3] * m.m[1][1] * m.m[3][0]) * invDet;
	matrix.m[2][3] = (m.m[0][0] * m.m[1][3] * m.m[2][1] + m.m[0][1] * m.m[1][0] * m.m[2][3] +
		m.m[0][3] * m.m[1][1] * m.m[2][0] - m.m[0][0] * m.m[1][1] * m.m[2][3] -
		m.m[0][1] * m.m[1][3] * m.m[2][0] - m.m[0][3] * m.m[1][0] * m.m[2][1]) * invDet;
	matrix.m[3][0] = (m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[1][1] * m.m[2][0] * m.m[3][2] +
		m.m[1][2] * m.m[2][1] * m.m[3][0] - m.m[1][0] * m.m[2][1] * m.m[3][2] -
		m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1]) * invDet;
	matrix.m[3][1] = (m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] +
		m.m[0][2] * m.m[2][0] * m.m[3][1] - m.m[0][0] * m.m[2][2] * m.m[3][1] -
		m.m[0][1] * m.m[2][0] * m.m[3][2] - m.m[0][2] * m.m[2][1] * m.m[3][0]) * invDet;
	matrix.m[3][2] = (m.m[0][0] * m.m[1][2] * m.m[3][1] + m.m[0][1] * m.m[1][0] * m.m[3][2] +
		m.m[0][2] * m.m[1][1] * m.m[3][0] - m.m[0][0] * m.m[1][1] * m.m[3][2] -
		m.m[0][1] * m.m[1][2] * m.m[3][0] - m.m[0][2] * m.m[1][0] * m.m[3][1]) * invDet;
	matrix.m[3][3] = (m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] +
		m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][0] * m.m[1][2] * m.m[2][1] -
		m.m[0][1] * m.m[1][0] * m.m[2][2] - m.m[0][2] * m.m[1][1] * m.m[2][0]) * invDet;
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

Matrix4x4 Matrix4x4::MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 scaleMatrix = {
		scale.x, 0.0f, 0.0f ,0.0f,
		0.0f, scale.y, 0.0f, 0.0f,
		0.0f, 0.0f, scale.z, 0.0f,
		0.0f ,0.0f, 0.0f, 1.0f
	};
	return scaleMatrix;
}

Matrix4x4 Matrix4x4::MakePitchMatrix(float degree) {
	
	return MakePitchMatrixRadians(Math::DegToRad(degree));
}

Matrix4x4 Matrix4x4::MakeYawMatrix(float degree) {
	
	return MakeYawMatrixRadians(Math::DegToRad(degree));
}

Matrix4x4 Matrix4x4::MakeRollMatrix(float degree) {
	
	return MakeRollMatrixRadians(Math::DegToRad(degree));
}

Matrix4x4 Engine::Matrix4x4::MakePitchMatrixRadians(float radian) {
	float cosTheta = std::cos(radian);
	float sinTheta = std::sin(radian);
	Matrix4x4 pitchMatrix = {
		1.0f, 0.0f,0.0f,0.0f,
		0.0f, cosTheta, sinTheta, 0.0f,
		0.0f, -sinTheta, cosTheta, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return pitchMatrix;
}

Matrix4x4 Engine::Matrix4x4::MakeYawMatrixRadians(float radian) {
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

Matrix4x4 Engine::Matrix4x4::MakeRollMatrixRadians(float radian) {
	float cosTheta = std::cos(radian);
	float sinTheta = std::sin(radian);
	Matrix4x4 rollMatrix = {
		cosTheta, sinTheta, 0.0f, 0.0f,
		-sinTheta, cosTheta, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return rollMatrix;
}

Matrix4x4 Matrix4x4::MakeRotateMatrix(const Vector3& rotateDegrees) {

	return MakeRotateMatrixRadians(Math::DegToRad(rotateDegrees));
}

Matrix4x4 Engine::Matrix4x4::MakeRotateMatrixRadians(const Vector3& rotateRadians) {

	Matrix4x4 pitchMatrix = MakePitchMatrixRadians(rotateRadians.x);
	Matrix4x4 yawMatrix = MakeYawMatrixRadians(rotateRadians.y);
	Matrix4x4 rollMatrix = MakeRollMatrixRadians(rotateRadians.z);
	Matrix4x4 rotateMatrix = pitchMatrix * yawMatrix * rollMatrix;
	return rotateMatrix;
}

Matrix4x4 Matrix4x4::MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 translateMatrix = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	translate.x, translate.y, translate.z, 1.0f
	};
	return translateMatrix;
}

Matrix4x4 Matrix4x4::MakeAffineMatrix(const Vector3& scale, const Vector3& rotateDegrees, const Vector3& translate) {
	
	return MakeAffineMatrixRadians(scale, Math::DegToRad(rotateDegrees), translate);
}

Matrix4x4 Engine::Matrix4x4::MakeAffineMatrixRadians(const Vector3& scale, const Vector3& rotateRadians, const Vector3& translate) {
	Matrix4x4 matrix = {};
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateMatrix = MakeRotateMatrixRadians(rotateRadians);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
	matrix = scaleMatrix * rotateMatrix;
	matrix = matrix * translateMatrix;
	return matrix;
}

Matrix4x4 Matrix4x4::MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
	Matrix4x4 matrix = {};
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateMatrix = Quaternion::MakeRotateMatrix(rotate);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
	matrix = scaleMatrix * rotateMatrix;
	matrix = matrix * translateMatrix;
	return matrix;
}

Matrix4x4 Matrix4x4::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 matrix = Zero();
	matrix.m[0][0] = 2.0f / (right - left);
	matrix.m[1][1] = 2.0f / (top - bottom);
	matrix.m[2][2] = 1.0f / (farClip - nearClip);
	matrix.m[3][0] = (left + right) / (left - right);
	matrix.m[3][1] = (top + bottom) / (bottom - top);
	matrix.m[3][2] = nearClip / (nearClip - farClip);
	matrix.m[3][3] = 1.0f;
	return matrix;
}

Matrix4x4 Matrix4x4::MakePerspectiveFovMatrix(float fovYDegrees, float aspectRatio, float nearClip, float farClip) {
	
	return MakePerspectiveFovMatrixRadians(Math::DegToRad(fovYDegrees), aspectRatio, nearClip, farClip);
}

Matrix4x4 Engine::Matrix4x4::MakePerspectiveFovMatrixRadians(float fovYRadians, float aspectRatio, float nearClip, float farClip) {

	Matrix4x4 matrix = Zero();
	matrix.m[0][0] = 1.0f / (aspectRatio * std::tanf(fovYRadians / 2.0f));
	matrix.m[1][1] = 1.0f / std::tanf(fovYRadians / 2.0f);
	matrix.m[2][2] = farClip / (farClip - nearClip);
	matrix.m[2][3] = 1.0f;
	matrix.m[3][2] = (-farClip * nearClip) / (farClip - nearClip);
	return matrix;

}

Matrix4x4 Matrix4x4::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 matrix = Zero();
	matrix.m[0][0] = width / 2.0f;
	matrix.m[1][1] = -height / 2.0f;
	matrix.m[2][2] = maxDepth - minDepth;
	matrix.m[3][0] = left + width / 2.0f;
	matrix.m[3][1] = top + height / 2.0f;
	matrix.m[3][2] = minDepth;
	matrix.m[3][3] = 1.0f;
	return matrix;
}

Vector3 Matrix4x4::GetTranslationValue() const {
	return Vector3(m[3][0], m[3][1], m[3][2]);
}