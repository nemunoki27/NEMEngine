#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <vector>
#include <algorithm>
#include <numbers>
// json
#include <json.hpp>

namespace Engine {

	// front
	struct Vector3;
	struct Matrix4x4;

	//============================================================================
	//	Quaternion structure
	//============================================================================
	struct Quaternion final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		float x, y, z, w;

		Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
		Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

		//--------- operators ----------------------------------------------------

		Quaternion operator+(const Quaternion& other) const;
		Quaternion operator-(const Quaternion& other) const;
		Quaternion operator*(const Quaternion& other) const;
		Quaternion operator/(const Quaternion& other) const;

		Quaternion operator+(float scalar) const;
		Quaternion operator-(float scalar) const;
		Quaternion operator*(float scalar) const;
		Quaternion operator/(float scalar) const;

		Quaternion operator-() const;

		bool operator==(const Quaternion& other) const;
		bool operator!=(const Quaternion& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Quaternion FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		static Quaternion Identity();

		// 長さ
		static float Length(const Quaternion& q);
		float Length() const;
		// 正規化
		static Quaternion Normalize(const Quaternion& quaternion);
		Quaternion Normalize() const;

		// 内積
		static float Dot(const Quaternion& q0, const Quaternion& q1);
		// 共役
		static Quaternion Conjugate(const Quaternion& q);
		// 逆
		static Quaternion Inverse(const Quaternion& q);

		// 任意軸回転
		static Quaternion MakeAxisAngle(const Vector3& axis, float angle);

		// クォータニオンを回転行列に変換
		static Matrix4x4 MakeRotateMatrix(const Quaternion& q);

		// 補間
		static Quaternion Lerp(Quaternion q0, const Quaternion& q1, float lerpT);

		// 関数の説明
		static Quaternion FromToY(const Vector3& direction);

		//------------------------------------------------------------------------
		//	オイラー変換
		//	エンジン標準: degree
		//------------------------------------------------------------------------
		static Quaternion EulerToQuaternion(const Vector3& eulerDegrees);
		static Vector3 ToEulerAngles(const Quaternion& q);
		//------------------------------------------------------------------------
		//	内部用: radians明示
		//------------------------------------------------------------------------
		static Quaternion FromEulerRadians(const Vector3& eulerRadians);
		static Quaternion FromEulerDegrees(const Vector3& eulerDegrees);
		static Vector3 ToEulerRadians(const Quaternion& q);
		static Vector3 ToEulerDegrees(const Quaternion& q);

		// 近似比較
		static bool NearlyEqual(const Quaternion& q0, const Quaternion& q1);
	};
} // Engine
