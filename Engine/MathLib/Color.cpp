#include "Color.h"

using namespace Engine;

//============================================================================
//	Color classMethods
//============================================================================

Color Color::operator+(const Color& other) const {
	return Color(r + other.r, g + other.g, b + other.b, a + other.a);
}
Color Color::operator-(const Color& other) const {
	return Color(r - other.r, g - other.g, b - other.b, a - other.a);
}
Color Color::operator*(const Color& other) const {
	return Color(r * other.r, g * other.g, b * other.b, a * other.a);
}
Color Color::operator/(const Color& other) const {
	return Color(r / other.r, g / other.g, b / other.b, a / other.a);
}

Color& Color::operator+=(const Color& v) {
	r += v.r;
	g += v.g;
	b += v.b;
	a += v.a;
	return *this;
}
Color& Color::operator-=(const Color& v) {
	r -= v.r;
	g -= v.g;
	b -= v.b;
	a -= v.a;
	return *this;
}
Color& Color::operator*=(const Color& v) {
	r *= v.r;
	g *= v.g;
	b *= v.b;
	a *= v.a;
	return *this;
}
Color& Color::operator/=(const Color& v) {
	r /= v.r;
	g /= v.g;
	b /= v.b;
	a /= v.a;
	return *this;
}

Color Color::operator+(float scalar) const {
	return Color(r + scalar, g + scalar, b + scalar, a + scalar);
}
Color Color::operator-(float scalar) const {
	return Color(r - scalar, g - scalar, b - scalar, a - scalar);
}
Color Color::operator*(float scalar) const {
	return Color(r * scalar, g * scalar, b * scalar, a * scalar);
}
Color Color::operator/(float scalar) const {
	return Color(r / scalar, g / scalar, b / scalar, a / scalar);
}

Color& Color::operator+=(float scalar) {
	r += scalar;
	g += scalar;
	b += scalar;
	a += scalar;
	return *this;
}
Color& Color::operator-=(float scalar) {
	r -= scalar;
	g -= scalar;
	b -= scalar;
	a -= scalar;
	return *this;
}
Color& Color::operator*=(float scalar) {
	r *= scalar;
	g *= scalar;
	b *= scalar;
	a *= scalar;
	return *this;
}
Color& Color::operator/=(float scalar) {
	r /= scalar;
	g /= scalar;
	b /= scalar;
	a /= scalar;
	return *this;
}

Color Color::operator-() const {
	return Color(-r, -g, -b, -a);
}

bool Color::operator==(const Color& other) const {
	return r == other.r && g == other.g && b == other.b && a == other.a;
}
bool Color::operator!=(const Color& other) const {
	return !(*this == other);
}

nlohmann::json Color::ToJson() const {
	return nlohmann::json{ {"r", r}, {"g", g}, {"b", b}, {"a", a} };
}
Color Color::FromJson(const nlohmann::json& data) {
	Color color{};
	if (data.is_array() && data.size() == 4) {
		color.r = data[0].get<float>();
		color.g = data[1].get<float>();
		color.b = data[2].get<float>();
		color.a = data[3].get<float>();
	} else if (data.contains("r") && data.contains("g") &&
		data.contains("b") && data.contains("a")) {
		color.r = data["r"].get<float>();
		color.g = data["g"].get<float>();
		color.b = data["b"].get<float>();
		color.a = data["a"].get<float>();
	}
	return color;
}

void Color::Init() {
	this->r = 0.0f;
	this->g = 0.0f;
	this->b = 0.0f;
	this->a = 1.0f;
}

Color Color::Lerp(const Color& c0, const Color& c1, float lerpT) {
	return Color(std::lerp(c0.r, c1.r, lerpT), std::lerp(c0.g, c1.g, lerpT),
		std::lerp(c0.b, c1.b, lerpT), std::lerp(c0.a, c1.a, lerpT));
}

Color Color::FromHex(uint32_t hex) {
	Color color{};

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

float Color::SRGBToLinear(float value) {
	if (value <= 0.04045f) {
		return value / 12.92f;
	}
	return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

Color Color::White(float alpha) {

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
Color Color::Cyan(float alpha) {

	return Color(0.0f, 1.0f, 1.0f, alpha);
}
Color Color::Magenta(float alpha) {

	return Color(1.0f, 0.0f, 1.0f, alpha);
}