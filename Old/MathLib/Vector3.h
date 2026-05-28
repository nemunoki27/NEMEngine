#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <math.h>
#include <vector>
#include <cassert>

// json
#include <Externals/nlohmann/json.hpp>
// using
using Json = nlohmann::json;

namespace SakuEngine {

	// front
	template <typename tValue>
	struct Keyframe;
	class Matrix4x4;

	//============================================================================
	//	Vector3 class
	//============================================================================
	class Vector3 final {
	public:

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

		Json ToJson() const;
		static Vector3 FromJson(const Json& data);

		//--------- functions ----------------------------------------------------

		void Init();

		float Length() const;

		Vector3 Normalize() const;

		static Vector3 AnyInit(float value);

		static Vector3 RotateVector(const Vector3& baseDirection, float angleRad);

		static Vector3 Cross(const Vector3& v0, const Vector3& v1);
		static float Dot(const Vector3& v0, const Vector3& v1);

		static float Length(const Vector3& v);

		static Vector3 Normalize(const Vector3& v);

		static Vector3 CalculateValue(const std::vector<Keyframe<Vector3>>& keyframes, float time);

		static Vector3 Lerp(const Vector3& v0, const Vector3& v1, float t);
		static Vector3 Lerp(const Vector3& v0, const Vector3& v1, const Vector3& lerpT);

		static Vector3 Reflect(const Vector3& input, const Vector3& normal);

		static Vector3 Transform(const Vector3& v, const Matrix4x4& matrix);

		static Vector3 TransferNormal(const Vector3& v, const Matrix4x4& m);

		static Vector3 Projection(const Vector3& v0, const Vector3& v1);

		static Vector3 ClosestPointOnLine(const Vector3& point, const Vector3& origin, const Vector3& diff);
		static Vector3 ClosestPointOnRay(const Vector3& point, const Vector3& origin, const Vector3& diff);
		static Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& origin, const Vector3& diff);
	};

	Vector3 operator*(float scalar, const Vector3& v);
	Vector3 operator-(float scalar, const Vector3& v);
	Vector3 operator/(float scalar, const Vector3& v);
}; // SakuEngine
