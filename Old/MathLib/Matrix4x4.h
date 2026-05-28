#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector3.h>

namespace SakuEngine {

	// front
	class Quaternion;

	//============================================================================
	//	Matrix4x4 class
	//============================================================================
	class Matrix4x4 final {
	public:

		float m[4][4];

		//--------- operators ----------------------------------------------------

		Matrix4x4 operator+(const Matrix4x4& other) const;
		Matrix4x4 operator-(const Matrix4x4& other) const;
		Matrix4x4 operator*(const Matrix4x4& other) const;
		Matrix4x4 operator/(float scalar) const;

		Matrix4x4& operator+=(const Matrix4x4& other);
		Matrix4x4& operator-=(const Matrix4x4& other);
		Matrix4x4& operator*=(const Matrix4x4& other);
		Matrix4x4& operator/=(const Matrix4x4& other);

		Matrix4x4& operator=(const Matrix4x4& other);

		bool operator==(const Matrix4x4& other) const;

		//--------- functions ----------------------------------------------------

		static Matrix4x4 Zero();

		static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

		static Matrix4x4 Inverse(const Matrix4x4& m);

		static Matrix4x4 Transpose(const Matrix4x4& m);

		static Matrix4x4 MakeIdentity4x4();

		static Matrix4x4 MakeScaleMatrix(const SakuEngine::Vector3& scale);

		static Matrix4x4 MakePitchMatrix(float radian);

		static Matrix4x4 MakeYawMatrix(float radian);

		static Matrix4x4 MakeRollMatrix(float radian);

		static Matrix4x4 MakeRotateMatrix(const SakuEngine::Vector3& rotate);

		static Matrix4x4 MakeTranslateMatrix(const SakuEngine::Vector3& translate);

		static Matrix4x4 MakeAffineMatrix(const SakuEngine::Vector3& scale, const SakuEngine::Vector3& rotate, const SakuEngine::Vector3& translate);

		static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
		static Matrix4x4 MakeShadowOrthographicMatrix(float width, float height, float nearClip, float farClip);

		static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

		static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

		static Matrix4x4 MakeAxisAffineMatrix(const SakuEngine::Vector3& scale, const Quaternion& rotate, const SakuEngine::Vector3& translate);

		Vector3 TransformPoint(const SakuEngine::Vector3& point) const;

		Vector3 GetTranslationValue() const;
		Vector3 GetRotationValue() const;
	};

}; // SakuEngine
