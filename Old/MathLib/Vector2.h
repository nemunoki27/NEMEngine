#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <vector>
// json
#include <Externals/nlohmann/json.hpp>
// using
using Json = nlohmann::json;

//============================================================================
//	Vector2 class
//============================================================================
namespace SakuEngine {

	class Vector2 final {
	public:

		float x, y;

		Vector2() : x(0.0f), y(0.0f) {}
		Vector2(float x, float y) : x(x), y(y) {}

		//--------- operators ----------------------------------------------------

		Vector2 operator+(const Vector2& other) const;
		Vector2 operator-(const Vector2& other) const;
		Vector2 operator*(const Vector2& other) const;
		Vector2 operator/(const Vector2& other) const;

		Vector2& operator+=(const Vector2& v);
		Vector2& operator-=(const Vector2& v);
		Vector2& operator*=(const Vector2& v);
		Vector2& operator/=(const Vector2& v);

		Vector2 operator*(float scalar) const;
		Vector2 operator/(float scalar) const;

		Vector2& operator*=(float scalar);

		Vector2 operator-() const;

		bool operator==(const Vector2& other) const;
		bool operator!=(const Vector2& other) const;

		bool operator<=(const Vector2& other) const;
		bool operator>=(const Vector2& other) const;

		//----------- json -------------------------------------------------------

		Json ToJson() const;
		static Vector2 FromJson(const Json& data);

		//--------- functions ----------------------------------------------------

		void Init();

		float Length() const;

		Vector2 Normalize() const;

		static Vector2 AnyInit(float value);

		static float Length(const SakuEngine::Vector2& v);

		static Vector2 Normalize(const SakuEngine::Vector2& v);

		static Vector2 Lerp(const SakuEngine::Vector2& v0, const SakuEngine::Vector2& v1, float t);
	};

	//============================================================================
	//	Vector2Int class
	//============================================================================
	class Vector2Int final {
	public:

		int32_t x, y;

		Vector2Int() : x(0), y(0) {}
		Vector2Int(int32_t x, int32_t y) : x(x), y(y) {}

		//--------- operators ----------------------------------------------------

		void operator+=(const Vector2Int& other);

		bool operator==(const Vector2Int& other) const;
		bool operator!=(const Vector2Int& other) const;

		//----------- json -------------------------------------------------------

		Json ToJson() const;
		static Vector2Int FromJson(const Json& data);
	};

	Vector2 operator*(float scalar, const Vector2& v);
	Vector2 operator/(float scalar, const Vector2& v);
}; // SakuEngine
