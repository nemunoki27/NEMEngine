#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <cmath>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	Color3 structure
	//============================================================================
	struct Color3 final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		float r, g, b;

		Color3() : r(0.0f), g(0.0f), b(0.0f) {}
		Color3(float r, float g, float b) : r(r), g(g), b(b) {}

		//--------- operators ----------------------------------------------------

		Color3 operator+(const Color3& other) const;
		Color3 operator-(const Color3& other) const;
		Color3 operator*(const Color3& other) const;
		Color3 operator/(const Color3& other) const;

		Color3& operator+=(const Color3& v);
		Color3& operator-=(const Color3& v);
		Color3& operator*=(const Color3& v);
		Color3& operator/=(const Color3& v);

		Color3 operator+(float scalar) const;
		Color3 operator-(float scalar) const;
		Color3 operator*(float scalar) const;
		Color3 operator/(float scalar) const;

		Color3& operator+=(float scalar);
		Color3& operator-=(float scalar);
		Color3& operator*=(float scalar);
		Color3& operator/=(float scalar);

		Color3 operator-() const;

		bool operator==(const Color3& other) const;
		bool operator!=(const Color3& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Color3 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		void Init();

		static Color3 Lerp(const Color3& c0, const Color3& c1, float lerpT);
		static Color3 FromHex(uint32_t hex);
		static float SRGBToLinear(float value);

		static Color3 White();
		static Color3 Black();
		static Color3 Red();
		static Color3 Green();
		static Color3 Blue();
		static Color3 Yellow();
		static Color3 Cyan();
		static Color3 Magenta();
	};

	//============================================================================
	//	Color4 structure
	//============================================================================
	struct Color4 final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		float r, g, b, a;

		Color4() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
		Color4(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}

		//--------- operators ----------------------------------------------------

		Color4 operator+(const Color4& other) const;
		Color4 operator-(const Color4& other) const;
		Color4 operator*(const Color4& other) const;
		Color4 operator/(const Color4& other) const;

		Color4& operator+=(const Color4& v);
		Color4& operator-=(const Color4& v);
		Color4& operator*=(const Color4& v);
		Color4& operator/=(const Color4& v);

		Color4 operator+(float scalar) const;
		Color4 operator-(float scalar) const;
		Color4 operator*(float scalar) const;
		Color4 operator/(float scalar) const;

		Color4& operator+=(float scalar);
		Color4& operator-=(float scalar);
		Color4& operator*=(float scalar);
		Color4& operator/=(float scalar);

		Color4 operator-() const;

		bool operator==(const Color4& other) const;
		bool operator!=(const Color4& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Color4 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		void Init();

		static Color4 Lerp(const Color4& c0, const Color4& c1, float lerpT);
		static Color4 FromHex(uint32_t hex);
		static float SRGBToLinear(float value);

		static Color4 White(float alpha = 1.0f);
		static Color4 Black(float alpha = 1.0f);
		static Color4 Red(float alpha = 1.0f);
		static Color4 Green(float alpha = 1.0f);
		static Color4 Blue(float alpha = 1.0f);
		static Color4 Yellow(float alpha = 1.0f);
		static Color4 Cyan(float alpha = 1.0f);
		static Color4 Magenta(float alpha = 1.0f);
	};
} // Engine