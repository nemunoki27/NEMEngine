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
	//	Color structure
	//============================================================================
	struct Color final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		float r, g, b, a;

		Color() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
		Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}

		//--------- operators ----------------------------------------------------

		Color operator+(const Color& other) const;
		Color operator-(const Color& other) const;
		Color operator*(const Color& other) const;
		Color operator/(const Color& other) const;

		Color& operator+=(const Color& v);
		Color& operator-=(const Color& v);
		Color& operator*=(const Color& v);
		Color& operator/=(const Color& v);

		Color operator+(float scalar) const;
		Color operator-(float scalar) const;
		Color operator*(float scalar) const;
		Color operator/(float scalar) const;

		Color& operator+=(float scalar);
		Color& operator-=(float scalar);
		Color& operator*=(float scalar);
		Color& operator/=(float scalar);

		Color operator-() const;

		bool operator==(const Color& other) const;
		bool operator!=(const Color& other) const;

		//----------- json -------------------------------------------------------

		nlohmann::json ToJson() const;
		static Color FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();

		// 補間
		static Color Lerp(const Color& c0, const Color& c1, float lerpT);

		// 16進数をfloatのRGBAに変換
		static Color FromHex(uint32_t hex);
		// SRGBからリニアRGBに変換
		static float SRGBToLinear(float value);

		// 定数
		static Color White(float alpha = 1.0f);
		static Color Black(float alpha = 1.0f);
		static	Color Red(float alpha = 1.0f);
		static Color Green(float alpha = 1.0f);
		static Color Blue(float alpha = 1.0f);
		static Color Yellow(float alpha = 1.0f);
		static Color Cyan(float alpha = 1.0f);
		static Color Magenta(float alpha = 1.0f);
	};
} // Engine
