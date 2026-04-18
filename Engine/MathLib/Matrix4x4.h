#pragma once

//============================================================================
//	include
//============================================================================

namespace Engine {

	// front
	struct Vector3;
	struct Quaternion;

	//============================================================================
	//	Matrix4x4 structure
	//============================================================================
	struct Matrix4x4 final {

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
		bool operator!=(const Matrix4x4& other) const;

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		static Matrix4x4 Identity();
		static Matrix4x4 Zero();

		// 逆/転置行列
		static Matrix4x4 Inverse(const Matrix4x4& m);
		static Matrix4x4 Transpose(const Matrix4x4& m);

		// 拡縮行列
		static Matrix4x4 MakeScaleMatrix(const Vector3& scale);
		//------------------------------------------------------------------------
		//	回転行列
		//	エンジン標準: degree
		//------------------------------------------------------------------------
		static Matrix4x4 MakePitchMatrix(float degree);
		static Matrix4x4 MakeYawMatrix(float degree);
		static Matrix4x4 MakeRollMatrix(float degree);
		static Matrix4x4 MakeRotateMatrix(const Vector3& rotateDegrees);
		//------------------------------------------------------------------------
		//	内部用: radians明示
		//------------------------------------------------------------------------
		static Matrix4x4 MakePitchMatrixRadians(float radian);
		static Matrix4x4 MakeYawMatrixRadians(float radian);
		static Matrix4x4 MakeRollMatrixRadians(float radian);
		static Matrix4x4 MakeRotateMatrixRadians(const Vector3& rotateRadians);
		// 平行移動行列
		static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
		// アフィン行列
		static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotateDegrees, const Vector3& translate);
		static Matrix4x4 MakeAffineMatrixRadians(const Vector3& scale, const Vector3& rotateRadians, const Vector3& translate);
		static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);

		// 射影行列
		static Matrix4x4 MakePerspectiveFovMatrix(float fovYDegrees, float aspectRatio, float nearClip, float farClip);
		static Matrix4x4 MakePerspectiveFovMatrixRadians(float fovYRadians, float aspectRatio, float nearClip, float farClip);
		static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
		static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

		// 行列から特定成分を取得
		Vector3 GetTranslationValue() const;
	};
} // Engine