#include "Vector4.h"

using namespace SakuEngine;

//============================================================================*/
//	namespace
//============================================================================*/

namespace {

	// 1ch分のsRGB → Linear
	inline float SRGBToLinear(float c) {

		return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
	}
}

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/MathLib/Vector3.h>

//============================================================================*/
//	Vector4 classMethods
//============================================================================*/

Vector4 Vector4::operator+(const Vector4& other) const {
	return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
}
Vector4 Vector4::operator-(const Vector4& other) const {
	return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
}
Vector4 Vector4::operator*(const Vector4& other) const {
	return Vector4(x * other.x, y * other.y, z * other.z, w * other.w);
}
Vector4 Vector4::operator/(const Vector4& other) const {
	return Vector4(x / other.x, y / other.y, z / other.z, w / other.w);
}

Vector4& Vector4::operator+=(const Vector4& v) {
	x += v.x;
	y += v.y;
	z += v.z;
	w += v.w;
	return *this;
}
Vector4& Vector4::operator-=(const Vector4& v) {
	x -= v.x;
	y -= v.y;
	z -= v.z;
	w -= v.w;
	return *this;
}

Vector4 Vector4::operator*(float scalar) const {
	return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
}
Vector4 operator*(float scalar, const Vector4& v) {
	return Vector4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}
Vector4 Vector4::operator/(float scalar) const {
	return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
}
Vector4 operator/(float scalar, const Vector4& v) {
	return Vector4(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
}

bool Vector4::operator==(const Vector4& other) const {
	return x == other.x && y == other.y && z == other.z;
}
bool Vector4::operator!=(const Vector4& other) const {
	return !(*this == other);
}

void Vector4::Init() {
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
	this->w = 0.0f;
}

//============================================================================*/
//	Color classMethods
//============================================================================*/

Color Color::operator+(const SakuEngine::Color& other) const {
	return Color(r + other.r, g + other.g, b + other.b, a + other.a);
}
Color Color::operator-(const SakuEngine::Color& other) const {
	return Color(r - other.r, g - other.g, b - other.b, a - other.a);
}
Color Color::operator*(const SakuEngine::Color& other) const {
	return Color(r * other.r, g * other.g, b * other.b, a * other.a);
}
Color Color::operator/(const SakuEngine::Color& other) const {
	return Color(r / other.r, g / other.g, b / other.b, a / other.a);
}

Color Color::operator-(float scalar) const {
	return Color(r - scalar, g - scalar, b - scalar, a - scalar);
}
Color operator-(float scalar, const SakuEngine::Color& v) {
	return Color(v.r - scalar, v.g - scalar, v.b - scalar, v.a - scalar);
}
Color Color::operator*(float scalar) const {
	return Color(r * scalar, g * scalar, b * scalar, a * scalar);
}
Color operator*(float scalar, const SakuEngine::Color& v) {
	return v * scalar;
}

Color Color::operator-() const {
	return Color(-r, -g, -b, -a);
}

void Color::FromRGB(const Vector3& rgb) {

	this->r = rgb.x;
	this->g = rgb.y;
	this->b = rgb.z;
}

Color Color::Convert(int color) {

	int r = (color >> 24) & 0xFF;
	int g = (color >> 16) & 0xFF;
	int b = (color >> 8) & 0xFF;
	int a = color & 0xFF;

	return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

Color Color::ToLinear() const {

	return Color(SRGBToLinear(r), SRGBToLinear(g), SRGBToLinear(b), a);
}

Color SakuEngine::Color::White(float alpha) {

	return Color(1.0f, 1.0f, 1.0f, alpha);
}
Color Color::Black(float alpha) {

	return Color(0.0f, 0.0f, 0.0f, alpha);
}
Color Color::Red(float alpha) {

	return Color(1.0f, 0.0f, 0.0f, alpha);
}
Color Color::Green(float alpha) {

	return Color(0.0f, 1.0f, 0.0f, alpha);
}
Color Color::Blue(float alpha) {

	return Color(0.0f, 0.0f, 1.0f, alpha);
}
Color Color::Yellow(float alpha) {

	return Color(1.0f, 1.0f, 0.0f, alpha);
}

Color SakuEngine::Color::Cyan(float alpha) {

	return Color(0.0f, 1.0f, 1.0f, alpha);
}

Color Color::Magenta(float alpha) {

	return Color(1.0f, 0.0f, 1.0f, alpha);
}

Color SakuEngine::Color::Lerp(const SakuEngine::Color& color0, const SakuEngine::Color& color1, float t) {

	float clampedT = std::clamp(t, 0.0f, 1.0f);
	return Color(std::lerp(color0.r, color1.r, clampedT),
		std::lerp(color0.g, color1.g, clampedT),
		std::lerp(color0.b, color1.b, clampedT),
		std::lerp(color0.a, color1.a, clampedT));
}

Json Color::ToJson() const {

	return Json{ {"r", r}, {"g", g}, {"b", b}, {"a", a} };
}

Color Color::FromJson(const Json& data) {

	Color color{};
	if (data.contains("r") && data.contains("g") &&
		data.contains("b") && data.contains("a")) {
		color.r = data["r"].get<float>();
		color.g = data["g"].get<float>();
		color.b = data["b"].get<float>();
		color.a = data["a"].get<float>();
	}
	return color;
}
