#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <vector>
#include <numbers>
#include <algorithm>

// json
#include <Externals/nlohmann/json.hpp>
// using
using Json = nlohmann::json;

namespace SakuEngine {

	// front
	class Vector3;
	class Matrix4x4;
	template <typename tValue>
	struct Keyframe;

	//============================================================================
	//	Quaternion class
	//============================================================================
	class Quaternion final {
	public:

		float x, y, z, w;

		//--------- operators ----------------------------------------------------

		Quaternion operator+(const Quaternion& other) const;
		Quaternion operator-(const Quaternion& other) const;
		Quaternion operator*(const Quaternion& other) const;
		Quaternion operator-() const;

		Quaternion operator*(float scalar) const;
		Vector3 operator*(const Vector3& v) const;

		bool operator==(const Quaternion& other) const;

		//----------- json -------------------------------------------------------

		Json ToJson() const;
		static Quaternion FromJson(const Json& data);

		//--------- functions ----------------------------------------------------

		void Init();

		Quaternion Normalize();

		static Quaternion EulerToQuaternion(const SakuEngine::Vector3& euler);

		static SakuEngine::Vector3 ToEulerAngles(const Quaternion& quaternion);

		static Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs);

		static Quaternion Identity();

		static Quaternion Conjugate(const Quaternion& quaternion);

		static float Norm(const Quaternion& quaternion);

		static Quaternion Normalize(const Quaternion& quaternion);

		static Quaternion Inverse(const Quaternion& quaternion);

		static Quaternion MakeAxisAngle(const SakuEngine::Vector3& axis, float angle);

		static SakuEngine::Vector3 RotateVector(const SakuEngine::Vector3& vector, const Quaternion& quaternion);

		static Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion);

		static Quaternion Slerp(Quaternion q0, const Quaternion& q1, float t);

		static float Dot(const Quaternion& q0, const Quaternion& q1);

		static Quaternion CalculateValue(const std::vector<Keyframe<Quaternion>>& keyframes, float time);

		static Quaternion LookRotation(const SakuEngine::Vector3& forward, const SakuEngine::Vector3& up);

		static Quaternion LookAt(const SakuEngine::Vector3& from, const SakuEngine::Vector3& to, const SakuEngine::Vector3& up);

		static Quaternion FromToRotation(const SakuEngine::Vector3& from, const SakuEngine::Vector3& to);

		static Quaternion FromRotationMatrix(const Matrix4x4& m);

		static Quaternion FromToY(const SakuEngine::Vector3& direction);

		static Quaternion LookTarget(const SakuEngine::Vector3& from, const SakuEngine::Vector3& to, const SakuEngine::Vector3& axis,
			const Quaternion& rotation, float lerpRate);

		static Quaternion ExtractTwistX(const Quaternion& qNorm);
		static Quaternion ExtractTwistZ(const Quaternion& qNorm);
	};

	Quaternion operator*(float scalar, const Quaternion& q);
}; // SakuEngine