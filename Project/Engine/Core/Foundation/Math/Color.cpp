#include "Color.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

using namespace Engine;

//============================================================================
//	Color3 classMethods
//============================================================================
Color3 Color3::operator+(const Color3& other) const {

	// RGBを成分ごとに加算
	return Color3(r + other.r, g + other.g, b + other.b);
}

Color3 Color3::operator-(const Color3& other) const {

	// RGBを成分ごとに減算
	return Color3(r - other.r, g - other.g, b - other.b);
}

Color3 Color3::operator*(const Color3& other) const {

	// RGBを成分ごとに乗算
	return Color3(r * other.r, g * other.g, b * other.b);
}

Color3 Color3::operator/(const Color3& other) const {

	// RGBを成分ごとに除算
	return Color3(r / other.r, g / other.g, b / other.b);
}

Color3& Color3::operator+=(const Color3& v) {

	// RGBを成分ごとに加算
	r += v.r;
	g += v.g;
	b += v.b;
	return *this;
}

Color3& Color3::operator-=(const Color3& v) {

	// RGBを成分ごとに減算
	r -= v.r;
	g -= v.g;
	b -= v.b;
	return *this;
}

Color3& Color3::operator*=(const Color3& v) {

	// RGBを成分ごとに乗算
	r *= v.r;
	g *= v.g;
	b *= v.b;
	return *this;
}

Color3& Color3::operator/=(const Color3& v) {

	// RGBを成分ごとに除算
	r /= v.r;
	g /= v.g;
	b /= v.b;
	return *this;
}

Color3 Color3::operator+(float scalar) const {

	// RGBを成分ごとに加算
	return Color3(r + scalar, g + scalar, b + scalar);
}

Color3 Color3::operator-(float scalar) const {

	// RGBを成分ごとに減算
	return Color3(r - scalar, g - scalar, b - scalar);
}

Color3 Color3::operator*(float scalar) const {

	// RGBを成分ごとに乗算
	return Color3(r * scalar, g * scalar, b * scalar);
}

Color3 Color3::operator/(float scalar) const {

	// RGBを成分ごとに除算
	return Color3(r / scalar, g / scalar, b / scalar);
}

Color3& Color3::operator+=(float scalar) {

	// RGBを成分ごとに加算
	r += scalar;
	g += scalar;
	b += scalar;
	return *this;
}

Color3& Color3::operator-=(float scalar) {

	// RGBを成分ごとに減算
	r -= scalar;
	g -= scalar;
	b -= scalar;
	return *this;
}

Color3& Color3::operator*=(float scalar) {

	// RGBを成分ごとに乗算
	r *= scalar;
	g *= scalar;
	b *= scalar;
	return *this;
}

Color3& Color3::operator/=(float scalar) {

	// RGBを成分ごとに除算
	r /= scalar;
	g /= scalar;
	b /= scalar;
	return *this;
}

Color3 Color3::operator-() const {

	// 各成分の符号を反転
	return Color3(-r, -g, -b);
}

bool Color3::operator==(const Color3& other) const {

	// 全成分の一致を比較
	return r == other.r && g == other.g && b == other.b;
}

bool Color3::operator!=(const Color3& other) const {

	// 成分の違いを比較
	return !(*this == other);
}

void Color3::Init() {

	// 既定の成分へ戻す
	r = 0.0f;
	g = 0.0f;
	b = 0.0f;
}

Color3 Color3::Lerp(const Color3& c0, const Color3& c1, float lerpT) {

	// 各成分を線形補間
	return Color3(std::lerp(c0.r, c1.r, lerpT), std::lerp(c0.g, c1.g, lerpT), std::lerp(c0.b, c1.b, lerpT));
}

Color3 Color3::FromHex(uint32_t hex) {

	// 16進色のRGBだけを線形化
	const Color4 color = Color4::FromHex(hex);
	return Color3(color.r, color.g, color.b);
}

float Color3::SRGBToLinear(float value) {

	// sRGBの成分を線形色へ変換
	return Color4::SRGBToLinear(value);
}

Color3 Color3::White() {

	// 白色を作る
	return Color3(1.0f, 1.0f, 1.0f);
}

Color3 Color3::Black() {

	// 黒色を作る
	return Color3(0.0f, 0.0f, 0.0f);
}

Color3 Color3::Red() {

	// 赤色を作る
	return Color3(1.0f, 0.0f, 0.0f);
}

Color3 Color3::Green() {

	// 緑色を作る
	return Color3(0.0f, 1.0f, 0.0f);
}

Color3 Color3::Blue() {

	// 青色を作る
	return Color3(0.0f, 0.0f, 1.0f);
}

Color3 Color3::Yellow() {

	// 黄色を作る
	return Color3(1.0f, 1.0f, 0.0f);
}

Color3 Color3::Cyan() {

	// 水色を作る
	return Color3(0.0f, 1.0f, 1.0f);
}

Color3 Color3::Magenta() {

	// 紫色を作る
	return Color3(1.0f, 0.0f, 1.0f);
}

//============================================================================
//	Color4 classMethods
//============================================================================
Color4 Color4::operator+(const Color4& other) const {

	// RGBAを成分ごとに加算
	return Color4(r + other.r, g + other.g, b + other.b, a + other.a);
}

Color4 Color4::operator-(const Color4& other) const {

	// RGBAを成分ごとに減算
	return Color4(r - other.r, g - other.g, b - other.b, a - other.a);
}

Color4 Color4::operator*(const Color4& other) const {

	// RGBAを成分ごとに乗算
	return Color4(r * other.r, g * other.g, b * other.b, a * other.a);
}

Color4 Color4::operator/(const Color4& other) const {

	// RGBAを成分ごとに除算
	return Color4(r / other.r, g / other.g, b / other.b, a / other.a);
}

Color4& Color4::operator+=(const Color4& v) {

	// RGBAを成分ごとに加算
	r += v.r;
	g += v.g;
	b += v.b;
	a += v.a;
	return *this;
}

Color4& Color4::operator-=(const Color4& v) {

	// RGBAを成分ごとに減算
	r -= v.r;
	g -= v.g;
	b -= v.b;
	a -= v.a;
	return *this;
}

Color4& Color4::operator*=(const Color4& v) {

	// RGBAを成分ごとに乗算
	r *= v.r;
	g *= v.g;
	b *= v.b;
	a *= v.a;
	return *this;
}

Color4& Color4::operator/=(const Color4& v) {

	// RGBAを成分ごとに除算
	r /= v.r;
	g /= v.g;
	b /= v.b;
	a /= v.a;
	return *this;
}

Color4 Color4::operator+(float scalar) const {

	// RGBAを成分ごとに加算
	return Color4(r + scalar, g + scalar, b + scalar, a + scalar);
}

Color4 Color4::operator-(float scalar) const {

	// RGBAを成分ごとに減算
	return Color4(r - scalar, g - scalar, b - scalar, a - scalar);
}

Color4 Color4::operator*(float scalar) const {

	// RGBAを成分ごとに乗算
	return Color4(r * scalar, g * scalar, b * scalar, a * scalar);
}

Color4 Color4::operator/(float scalar) const {

	// RGBAを成分ごとに除算
	return Color4(r / scalar, g / scalar, b / scalar, a / scalar);
}

Color4& Color4::operator+=(float scalar) {

	// RGBAを成分ごとに加算
	r += scalar;
	g += scalar;
	b += scalar;
	a += scalar;
	return *this;
}

Color4& Color4::operator-=(float scalar) {

	// RGBAを成分ごとに減算
	r -= scalar;
	g -= scalar;
	b -= scalar;
	a -= scalar;
	return *this;
}

Color4& Color4::operator*=(float scalar) {

	// RGBAを成分ごとに乗算
	r *= scalar;
	g *= scalar;
	b *= scalar;
	a *= scalar;
	return *this;
}

Color4& Color4::operator/=(float scalar) {

	// RGBAを成分ごとに除算
	r /= scalar;
	g /= scalar;
	b /= scalar;
	a /= scalar;
	return *this;
}

Color4 Color4::operator-() const {

	// 各成分の符号を反転
	return Color4(-r, -g, -b, -a);
}

bool Color4::operator==(const Color4& other) const {

	// 全成分の一致を比較
	return r == other.r && g == other.g && b == other.b && a == other.a;
}

bool Color4::operator!=(const Color4& other) const {

	// 成分の違いを比較
	return !(*this == other);
}

void Color4::Init() {

	// 既定の成分へ戻す
	r = 0.0f;
	g = 0.0f;
	b = 0.0f;
	a = 1.0f;
}

Color4 Color4::Lerp(const Color4& c0, const Color4& c1, float lerpT) {

	// 各成分を線形補間
	return Color4(
		std::lerp(c0.r, c1.r, lerpT), std::lerp(c0.g, c1.g, lerpT), std::lerp(c0.b, c1.b, lerpT), std::lerp(c0.a, c1.a, lerpT));
}

Color4 Color4::FromHex(uint32_t hex) {

	// 16進色のRGBだけを線形化
	Color4 color{};

	const float sr = static_cast<float>((hex >> 24) & 0xFF) / 255.0f;
	const float sg = static_cast<float>((hex >> 16) & 0xFF) / 255.0f;
	const float sb = static_cast<float>((hex >> 8) & 0xFF) / 255.0f;
	const float sa = static_cast<float>(hex & 0xFF) / 255.0f;
	color.r = SRGBToLinear(sr);
	color.g = SRGBToLinear(sg);
	color.b = SRGBToLinear(sb);
	color.a = sa;
	return color;
}

float Color4::SRGBToLinear(float value) {

	// sRGBの成分を線形色へ変換
	if (value <= 0.04045f) {
		return value / 12.92f;
	}
	return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

Color4 Color4::White(float alpha) {

	// 白色を作る
	return Color4(1.0f, 1.0f, 1.0f, alpha);
}

Color4 Color4::Black(float alpha) {

	// 黒色を作る
	return Color4(0.0f, 0.0f, 0.0f, alpha);
}

Color4 Color4::Red(float alpha) {

	// 赤色を作る
	return Color4(1.0f, 0.0f, 0.0f, alpha);
}

Color4 Color4::Green(float alpha) {

	// 緑色を作る
	return Color4(0.0f, 1.0f, 0.0f, alpha);
}

Color4 Color4::Blue(float alpha) {

	// 青色を作る
	return Color4(0.0f, 0.0f, 1.0f, alpha);
}

Color4 Color4::Yellow(float alpha) {

	// 黄色を作る
	return Color4(1.0f, 1.0f, 0.0f, alpha);
}

Color4 Color4::Cyan(float alpha) {

	// 水色を作る
	return Color4(0.0f, 1.0f, 1.0f, alpha);
}

Color4 Color4::Magenta(float alpha) {

	// 紫色を作る
	return Color4(1.0f, 0.0f, 1.0f, alpha);
}
