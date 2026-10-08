#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	Color3 structure
	//	RGB色の演算と保存変換
	//============================================================================
	struct Color3 final {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		float r; // 赤成分
		float g; // 緑成分
		float b; // 青成分

		//--------- functions ----------------------------------------------------

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

		// 成分名をキーにして保存する
		nlohmann::json ToJson() const;
		// 配列と成分名付きの保存値を読み込む
		static Color3 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 既定の色へ初期化する
		void Init();

		// 各成分を線形補間する
		static Color3 Lerp(const Color3& c0, const Color3& c1, float lerpT);
		// 16進RGBAのRGBを線形色へ変換する
		static Color3 FromHex(uint32_t hex);
		// sRGBの成分を線形色へ変換する
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
	//	RGBA色の演算と保存変換
	//============================================================================
	struct Color4 final {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		float r; // 赤成分
		float g; // 緑成分
		float b; // 青成分
		float a; // 透明度

		//--------- functions ----------------------------------------------------

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

		// 成分名をキーにして保存する
		nlohmann::json ToJson() const;
		// 配列と成分名付きの保存値を読み込む
		static Color4 FromJson(const nlohmann::json& data);

		//--------- functions ----------------------------------------------------

		// 既定の色へ初期化する
		void Init();

		// 各成分を線形補間する
		static Color4 Lerp(const Color4& c0, const Color4& c1, float lerpT);
		// 16進RGBAのRGBを線形色へ変換する
		static Color4 FromHex(uint32_t hex);
		// sRGBの成分を線形色へ変換する
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
}