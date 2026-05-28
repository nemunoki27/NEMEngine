#include "Vector2.h"

using namespace SakuEngine;

//============================================================================*/
//	Vector2 classMethods
//============================================================================*/

Vector2 Vector2::operator+(const Vector2& other) const {
	return Vector2(x + other.x, y + other.y);
}
Vector2 Vector2::operator-(const Vector2& other) const {
	return Vector2(x - other.x, y - other.y);
}
Vector2 Vector2::operator*(const Vector2& other) const {
	return Vector2(x * other.x, y * other.y);
}
Vector2 Vector2::operator/(const Vector2& other) const {
	return Vector2(x / other.x, y / other.y);
}

Vector2& Vector2::operator+=(const Vector2& v) {
	x += v.x;
	y += v.y;
	return *this;
}
Vector2& Vector2::operator-=(const Vector2& v) {
	x -= v.x;
	y -= v.y;
	return *this;
}

Vector2& Vector2::operator*=(const Vector2& v) {
	x *= v.x;
	y *= v.y;
	return *this;
}

Vector2& Vector2::operator/=(const Vector2& v) {
	x /= v.x;
	y /= v.y;
	return *this;
}

Vector2 Vector2::operator*(float scalar) const {
	return Vector2(x * scalar, y * scalar);
}
Vector2 Vector2::operator/(float scalar) const {
	return Vector2(x / scalar, y / scalar);
}
namespace SakuEngine {

	Vector2 operator*(float scalar, const Vector2& v) {
		return Vector2(v.x * scalar, v.y * scalar);
	}

	Vector2 operator/(float scalar, const Vector2& v) {
		return Vector2(v.x / scalar, v.y / scalar);
	}
}

Vector2& Vector2::operator*=(float scalar) {
	x *= scalar;
	y *= scalar;
	return *this;
}

Vector2 Vector2::operator-() const {
	return Vector2(-x, -y);
}

bool Vector2::operator==(const Vector2& other) const {
	return x == other.x && y == other.y;
}
bool Vector2::operator!=(const Vector2& other) const {
	return !(*this == other);
}

bool Vector2::operator<=(const Vector2& other) const {
	return this->Length() <= other.Length();
}

bool Vector2::operator>=(const Vector2& other) const {
	return this->Length() >= other.Length();
}

Json Vector2::ToJson() const {
	return Json{ {"x", x}, {"y", y} };
}

Vector2 SakuEngine::Vector2::FromJson(const Json& data) {

	if (data.empty()) {
		return Vector2{};
	}

	Vector2 v{};
	if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
	}
	return v;
}

void Vector2::Init() {

	this->x = 0.0f;
	this->y = 0.0f;
}

float Vector2::Length() const {
	return std::sqrtf(x * x + y * y);
}

Vector2 SakuEngine::Vector2::Normalize() const {
	float length = this->Length();
	if (length == 0.0f) {
		return Vector2(0.0f, 0.0f);
	}
	return Vector2(x / length, y / length);
}

Vector2 SakuEngine::Vector2::AnyInit(float value) {

	return Vector2(value, value);
}

float Vector2::Length(const SakuEngine::Vector2& v) {
	return std::sqrtf(v.x * v.x + v.y * v.y);
}

Vector2 SakuEngine::Vector2::Normalize(const SakuEngine::Vector2& v) {

	float length = Vector2::Length(v);
	if (length == 0.0f) {
		return Vector2(0.0f, 0.0f);
	}
	return Vector2(v.x / length, v.y / length);
}

Vector2 SakuEngine::Vector2::Lerp(const SakuEngine::Vector2& v0, const SakuEngine::Vector2& v1, float t) {

	return Vector2(std::lerp(v0.x, v1.x, t), std::lerp(v0.y, v1.y, t));
}

//============================================================================*/
//	Vector2Int classMethods
//============================================================================*/

void Vector2Int::operator+=(const Vector2Int& other) {

	this->x += other.x;
	this->y += other.y;
}

bool Vector2Int::operator==(const Vector2Int& other) const {

	return this->x == other.x && this->y == other.y;
}

bool Vector2Int::operator!=(const Vector2Int& other) const {

	return !(*this == other);
}

Json Vector2Int::ToJson() const {
	return Json{ {"x", x}, {"y", y} };
}

Vector2Int Vector2Int::FromJson(const Json& data) {

	if (data.empty()) {
		return Vector2Int{};
	}

	Vector2Int v{};
	if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<int32_t>();
		v.y = data["y"].get<int32_t>();
	}
	return v;
}