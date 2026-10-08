#pragma once

//============================================================================
//	include
//============================================================================
// c++
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	Vector4 structure
	//	4成分の浮動小数点ベクトル
	//============================================================================
	struct Vector4 final {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		float x; // X成分
		float y; // Y成分
		float z; // Z成分
		float w; // W成分

		//--------- functions ----------------------------------------------------

		Vector4() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
		Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

		//--------- operators ----------------------------------------------------

		Vector4 operator+(const Vector4& other) const;
		Vector4 operator-(const Vector4& other) const;
		Vector4 operator*(const Vector4& other) const;
		Vector4 operator/(const Vector4& other) const;

		Vector4& operator+=(const Vector4& v);
		Vector4& operator-=(const Vector4& v);
		Vector4& operator*=(const Vector4& v);
		Vector4& operator/=(const Vector4& v);

		Vector4 operator+(float scalar) const;
		Vector4 operator-(float scalar) const;
		Vector4 operator*(float scalar) const;
		Vector4 operator/(float scalar) const;

		Vector4& operator+=(float scalar);
		Vector4& operator-=(float scalar);
		Vector4& operator*=(float scalar);
		Vector4& operator/=(float scalar);

		Vector4 operator-() const;

		bool operator==(const Vector4& other) const;
		bool operator!=(const Vector4& other) const;

		bool operator>=(const Vector4& other) const;
		bool operator<=(const Vector4& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Vector4 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
	};
}
