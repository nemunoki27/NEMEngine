#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <vector>
// json
#include <Externals/nlohmann/json.hpp>

namespace Engine {

	//============================================================================
	//	Vector2 structure
	//============================================================================
	struct Vector2 final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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

		Vector2 operator+(float scalar) const;
		Vector2 operator-(float scalar) const;
		Vector2 operator*(float scalar) const;
		Vector2 operator/(float scalar) const;

		Vector2& operator+=(float scalar);
		Vector2& operator-=(float scalar);
		Vector2& operator*=(float scalar);
		Vector2& operator/=(float scalar);

		Vector2 operator-() const;

		bool operator==(const Vector2& other) const;
		bool operator!=(const Vector2& other) const;

		bool operator>=(const Vector2& other) const;
		bool operator<=(const Vector2& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Vector2 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		void Init(float value);
		static Vector2 AnyInit(float value);

		// ベクトルの長さ
		static float Length(const Vector2& v);
		float Length() const;
		// 正規化
		static Vector2 Normalize(const Vector2& v);
		Vector2 Normalize() const;

		// 内積
		static float Dot(const Vector2& v0, const Vector2& v1);
		// 外積
		static Vector2 Cross(const Vector2& v0, const Vector2& v1);

		// 補間
		static Vector2 Lerp(const Vector2& v0, const Vector2& v1, float lerpT);
		static Vector2 Lerp(const Vector2& v0, const Vector2& v1, const Vector2& lerpT);
	};

	//============================================================================
	//	Vector2I structure
	//============================================================================
	struct Vector2I final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		int32_t x, y;

		Vector2I() : x(0), y(0) {}
		Vector2I(int32_t x, int32_t y) : x(x), y(y) {}

		//--------- operators ----------------------------------------------------

		Vector2I operator+(const Vector2I& other) const;
		Vector2I operator-(const Vector2I& other) const;
		Vector2I operator*(const Vector2I& other) const;
		Vector2I operator/(const Vector2I& other) const;

		Vector2I& operator+=(const Vector2I& v);
		Vector2I& operator-=(const Vector2I& v);
		Vector2I& operator*=(const Vector2I& v);
		Vector2I& operator/=(const Vector2I& v);

		Vector2I operator+(int32_t scalar) const;
		Vector2I operator-(int32_t scalar) const;
		Vector2I operator*(int32_t scalar) const;
		Vector2I operator/(int32_t scalar) const;

		Vector2I& operator+=(int32_t scalar);
		Vector2I& operator-=(int32_t scalar);
		Vector2I& operator*=(int32_t scalar);
		Vector2I& operator/=(int32_t scalar);

		Vector2I operator-() const;

		bool operator==(const Vector2I& other) const;
		bool operator!=(const Vector2I& other) const;

		bool operator>=(const Vector2I& other) const;
		bool operator<=(const Vector2I& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Vector2I FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		void Init(int32_t value);

		// uintに変換
		std::vector<uint32_t> ToUInt() const;
	};
} // Engine