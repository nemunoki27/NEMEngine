#include "Color.h"

using namespace Engine;

//============================================================================
//	Color3 classMethods
//============================================================================

Color3 Color3::operator+(const Color3& other) const {
	return Color3(r + other.r, g + other.g, b + other.b);
}
Color3 Color3::operator-(const Color3& other) const {
	return Color3(r - other.r, g - other.g, b - other.b);
}
Color3 Color3::operator*(const Color3& other) const {
	return Color3(r * other.r, g * other.g, b * other.b);
}
Color3 Color3::operator/(const Color3& other) const {
	return Color3(r / other.r, g / other.g, b / other.b);
}

Color3& Color3::operator+=(const Color3& v) {
	r += v.r;
	g += v.g;
	b += v.b;
	return *this;
}
Color3& Color3::operator-=(const Color3& v) {
	r -= v.r;
	g -= v.g;
	b -= v.b;
	return *this;
}
Color3& Color3::operator*=(const Color3& v) {
	r *= v.r;
	g *= v.g;
	b *= v.b;
	return *this;
}
Color3& Color3::operator/=(const Color3& v) {
	r /= v.r;
	g /= v.g;
	b /= v.b;
	return *this;
}

Color3 Color3::operator+(float scalar) const {
	return Color3(r + scalar, g + scalar, b + scalar);
}
Color3 Color3::operator-(float scalar) const {
	return Color3(r - scalar, g - scalar, b - scalar);
}
Color3 Color3::operator*(float scalar) const {
	return Color3(r * scalar, g * scalar, b * scalar);
}
Color3 Color3::operator/(float scalar) const {
	return Color3(r / scalar, g / scalar, b / scalar);
}

Color3& Color3::operator+=(float scalar) {
	r += scalar;
	g += scalar;
	b += scalar;
	return *this;
}
Color3& Color3::operator-=(float scalar) {
	r -= scalar;
	g -= scalar;
	b -= scalar;
	return *this;
}
Color3& Color3::operator*=(float scalar) {
	r *= scalar;
	g *= scalar;
	b *= scalar;
	return *this;
}
Color3& Color3::operator/=(float scalar) {
	r /= scalar;
	g /= scalar;
	b /= scalar;
	return *this;
}

Color3 Color3::operator-() const {
	return Color3(-r, -g, -b);
}

bool Color3::operator==(const Color3& other) const {
	return r == other.r && g == other.g && b == other.b;
}
bool Color3::operator!=(const Color3& other) const {
	return !(*this == other);
}

nlohmann::json Color3::ToJson() const {
	return nlohmann::json{ {"r", r}, {"g", g}, {"b", b} };
}
Color3 Color3::FromJson(const nlohmann::json& data) {
	Color3 color{};
	if (data.is_array() && data.size() == 3) {
		color.r = data[0].get<float>();
		color.g = data[1].get<float>();
		color.b = data[2].get<float>();
	} else if (data.contains("r") && data.contains("g") && data.contains("b")) {
		color.r = data["r"].get<float>();
		color.g = data["g"].get<float>();
		color.b = data["b"].get<float>();
	}
	return color;
}

void Color3::Init() {
	r = 0.0f;
	g = 0.0f;
	b = 0.0f;
}

Color3 Color3::Lerp(const Color3& c0, const Color3& c1, float lerpT) {
	return Color3(std::lerp(c0.r, c1.r, lerpT), std::lerp(c0.g, c1.g, lerpT),
		std::lerp(c0.b, c1.b, lerpT));
}

Color3 Color3::FromHex(uint32_t hex) {
	const Color4 color = Color4::FromHex(hex);
	return Color3(color.r, color.g, color.b);
}

float Color3::SRGBToLinear(float value) {
	return Color4::SRGBToLinear(value);
}

Color3 Color3::White() {
	return Color3(1.0f, 1.0f, 1.0f);
}
Color3 Color3::Black() {
	return Color3(0.0f, 0.0f, 0.0f);
}
Color3 Color3::Red() {
	return Color3(1.0f, 0.0f, 0.0f);
}
Color3 Color3::Green() {
	return Color3(0.0f, 1.0f, 0.0f);
}
Color3 Color3::Blue() {
	return Color3(0.0f, 0.0f, 1.0f);
}
Color3 Color3::Yellow() {
	return Color3(1.0f, 1.0f, 0.0f);
}
Color3 Color3::Cyan() {
	return Color3(0.0f, 1.0f, 1.0f);
}
Color3 Color3::Magenta() {
	return Color3(1.0f, 0.0f, 1.0f);
}

//============================================================================
//	Color4 classMethods
//============================================================================

Color4 Color4::operator+(const Color4& other) const {
	return Color4(r + other.r, g + other.g, b + other.b, a + other.a);
}
Color4 Color4::operator-(const Color4& other) const {
	return Color4(r - other.r, g - other.g, b - other.b, a - other.a);
}
Color4 Color4::operator*(const Color4& other) const {
	return Color4(r * other.r, g * other.g, b * other.b, a * other.a);
}
Color4 Color4::operator/(const Color4& other) const {
	return Color4(r / other.r, g / other.g, b / other.b, a / other.a);
}

Color4& Color4::operator+=(const Color4& v) {
	r += v.r;
	g += v.g;
	b += v.b;
	a += v.a;
	return *this;
}
Color4& Color4::operator-=(const Color4& v) {
	r -= v.r;
	g -= v.g;
	b -= v.b;
	a -= v.a;
	return *this;
}
Color4& Color4::operator*=(const Color4& v) {
	r *= v.r;
	g *= v.g;
	b *= v.b;
	a *= v.a;
	return *this;
}
Color4& Color4::operator/=(const Color4& v) {
	r /= v.r;
	g /= v.g;
	b /= v.b;
	a /= v.a;
	return *this;
}

Color4 Color4::operator+(float scalar) const {
	return Color4(r + scalar, g + scalar, b + scalar, a + scalar);
}
Color4 Color4::operator-(float scalar) const {
	return Color4(r - scalar, g - scalar, b - scalar, a - scalar);
}
Color4 Color4::operator*(float scalar) const {
	return Color4(r * scalar, g * scalar, b * scalar, a * scalar);
}
Color4 Color4::operator/(float scalar) const {
	return Color4(r / scalar, g / scalar, b / scalar, a / scalar);
}

Color4& Color4::operator+=(float scalar) {
	r += scalar;
	g += scalar;
	b += scalar;
	a += scalar;
	return *this;
}
Color4& Color4::operator-=(float scalar) {
	r -= scalar;
	g -= scalar;
	b -= scalar;
	a -= scalar;
	return *this;
}
Color4& Color4::operator*=(float scalar) {
	r *= scalar;
	g *= scalar;
	b *= scalar;
	a *= scalar;
	return *this;
}
Color4& Color4::operator/=(float scalar) {
	r /= scalar;
	g /= scalar;
	b /= scalar;
	a /= scalar;
	return *this;
}

Color4 Color4::operator-() const {
	return Color4(-r, -g, -b, -a);
}

bool Color4::operator==(const Color4& other) const {
	return r == other.r && g == other.g && b == other.b && a == other.a;
}
bool Color4::operator!=(const Color4& other) const {
	return !(*this == other);
}

nlohmann::json Color4::ToJson() const {
	return nlohmann::json{ {"r", r}, {"g", g}, {"b", b}, {"a", a} };
}
Color4 Color4::FromJson(const nlohmann::json& data) {
	Color4 color{};
	if (data.is_array() && data.size() == 4) {
		color.r = data[0].get<float>();
		color.g = data[1].get<float>();
		color.b = data[2].get<float>();
		color.a = data[3].get<float>();
	} else if (data.contains("r") && data.contains("g") && data.contains("b")) {
		color.r = data.value("r", 0.0f);
		color.g = data.value("g", 0.0f);
		color.b = data.value("b", 0.0f);
		color.a = data.value("a", 1.0f);
	}
	return color;
}

void Color4::Init() {
	r = 0.0f;
	g = 0.0f;
	b = 0.0f;
	a = 1.0f;
}

Color4 Color4::Lerp(const Color4& c0, const Color4& c1, float lerpT) {
	return Color4(std::lerp(c0.r, c1.r, lerpT), std::lerp(c0.g, c1.g, lerpT),
		std::lerp(c0.b, c1.b, lerpT), std::lerp(c0.a, c1.a, lerpT));
}

Color4 Color4::FromHex(uint32_t hex) {
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
	if (value <= 0.04045f) {
		return value / 12.92f;
	}
	return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

Color4 Color4::White(float alpha) {
	return Color4(1.0f, 1.0f, 1.0f, alpha);
}
Color4 Color4::Black(float alpha) {
	return Color4(0.0f, 0.0f, 0.0f, alpha);
}
Color4 Color4::Red(float alpha) {
	return Color4(1.0f, 0.0f, 0.0f, alpha);
}
Color4 Color4::Green(float alpha) {
	return Color4(0.0f, 1.0f, 0.0f, alpha);
}
Color4 Color4::Blue(float alpha) {
	return Color4(0.0f, 0.0f, 1.0f, alpha);
}
Color4 Color4::Yellow(float alpha) {
	return Color4(1.0f, 1.0f, 0.0f, alpha);
}
Color4 Color4::Cyan(float alpha) {
	return Color4(0.0f, 1.0f, 1.0f, alpha);
}
Color4 Color4::Magenta(float alpha) {
	return Color4(1.0f, 0.0f, 1.0f, alpha);
}
