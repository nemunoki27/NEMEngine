#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	// front
	struct Matrix4x4;

	//============================================================================
	//	Vector3 structure
	//============================================================================
	struct Vector3 final {

		float x, y, z;

		Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
		Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

		//--------- operators ----------------------------------------------------

		Vector3 operator+(const Vector3& other) const;
		Vector3 operator-(const Vector3& other) const;
		Vector3 operator*(const Vector3& other) const;
		Vector3 operator/(const Vector3& other) const;

		Vector3& operator+=(const Vector3& v);
		Vector3& operator-=(const Vector3& v);
		Vector3& operator*=(const Vector3& v);
		Vector3& operator/=(const Vector3& v);

		Vector3 operator+(float scalar) const;
		Vector3 operator-(float scalar) const;
		Vector3 operator*(float scalar) const;
		Vector3 operator/(float scalar) const;

		Vector3& operator+=(float scalar);
		Vector3& operator-=(float scalar);
		Vector3& operator*=(float scalar);
		Vector3& operator/=(float scalar);

		Vector3 operator-() const;

		bool operator==(const Vector3& other) const;
		bool operator!=(const Vector3& other) const;

		bool operator>=(const Vector3& other) const;
		bool operator<=(const Vector3& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Vector3 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		void Init(float value);
		static Vector3 AnyInit(float value);

		// ベクトルの長さ
		static float Length(const Vector3& v);
		float Length() const;
		// 正規化
		static Vector3 Normalize(const Vector3& v);
		Vector3 Normalize() const;

		// 内積
		static float Dot(const Vector3& v0, const Vector3& v1);
		// 外積
		static Vector3 Cross(const Vector3& v0, const Vector3& v1);

		// 補間
		static Vector3 Lerp(const Vector3& v0, const Vector3& v1, float lerpT);
		static Vector3 Lerp(const Vector3& v0, const Vector3& v1, const Vector3& lerpT);

		// inputをnormalに反射させたベクトルを返す
		static Vector3 Reflect(const Vector3& input, const Vector3& normal);

		// ベクトルを行列で変換
		static Vector3 Transform(const Vector3& v, const Matrix4x4& matrix);
		static Vector3 TransferNormal(const Vector3& v, const Matrix4x4& m);
		static Vector3 TransformPoint(const Vector3& v, const Matrix4x4& m);

		// v0をv1に射影したベクトルを返す
		static Vector3 Projection(const Vector3& v0, const Vector3& v1);

		// 近似比較
		static bool NearlyEqual(const Vector3& v0, const Vector3& v1);

		// アングルを参照角度に最も近い360度系へ寄せる
		static Vector3 MakeContinuousDegrees(const Vector3& rawEuler, const Vector3& referenceEuler);
	};
	Vector3 operator+(float scalar, const Vector3& v);
	Vector3 operator-(float scalar, const Vector3& v);
	Vector3 operator*(float scalar, const Vector3& v);
	Vector3 operator/(float scalar, const Vector3& v);

	//============================================================================
	//	Vector3I structure
	//============================================================================
	struct Vector3I final {

		int32_t x, y, z;

		Vector3I() : x(0), y(0), z(0) {}
		Vector3I(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}

		//--------- operators ----------------------------------------------------

		Vector3I operator+(const Vector3I& other) const;
		Vector3I operator-(const Vector3I& other) const;
		Vector3I operator*(const Vector3I& other) const;
		Vector3I operator/(const Vector3I& other) const;

		Vector3I& operator+=(const Vector3I& v);
		Vector3I& operator-=(const Vector3I& v);
		Vector3I& operator*=(const Vector3I& v);
		Vector3I& operator/=(const Vector3I& v);

		Vector3I operator+(int32_t scalar) const;
		Vector3I operator-(int32_t scalar) const;
		Vector3I operator*(int32_t scalar) const;
		Vector3I operator/(int32_t scalar) const;

		Vector3I& operator+=(int32_t scalar);
		Vector3I& operator-=(int32_t scalar);
		Vector3I& operator*=(int32_t scalar);
		Vector3I& operator/=(int32_t scalar);

		Vector3I operator-() const;

		bool operator==(const Vector3I& other) const;
		bool operator!=(const Vector3I& other) const;

		bool operator>=(const Vector3I& other) const;
		bool operator<=(const Vector3I& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Vector3I FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		void Init(int32_t value);

		// uintに変換
		std::vector<uint32_t> ToUInt() const;
	};
} // Engine
