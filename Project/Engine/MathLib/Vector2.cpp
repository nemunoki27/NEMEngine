#include "Vector2.h"

using namespace Engine;

//============================================================================
//	Vector2 structMethods
//============================================================================

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

Vector2 Vector2::operator+(float scalar) const {
	return Vector2(x + scalar, y + scalar);
}
Vector2 Vector2::operator-(float scalar) const {
	return Vector2(x - scalar, y - scalar);
}
Vector2 Vector2::operator*(float scalar) const {
	return Vector2(x * scalar, y * scalar);
}
Vector2 Vector2::operator/(float scalar) const {
	return Vector2(x / scalar, y / scalar);
}

Vector2& Vector2::operator+=(float scalar) {
	x += scalar;
	y += scalar;
	return *this;
}
Vector2& Vector2::operator-=(float scalar) {
	x -= scalar;
	y -= scalar;
	return *this;
}
Vector2& Vector2::operator*=(float scalar) {
	x *= scalar;
	y *= scalar;
	return *this;
}
Vector2& Vector2::operator/=(float scalar) {
	x /= scalar;
	y /= scalar;
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
bool Vector2::operator>=(const Vector2& other) const {
	return this->Length() >= other.Length();
}
bool Vector2::operator<=(const Vector2& other) const {
	return this->Length() <= other.Length();
}

nlohmann::json Vector2::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y} };
}
Vector2 Vector2::FromJson(const nlohmann::json& data) {
	Vector2 v{};
	if (data.is_array() && data.size() == 2) {
		v.x = data[0].get<float>();
		v.y = data[1].get<float>();
	} else if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
	}
	return v;
}

void Vector2::Init() {
	this->x = 0.0f;
	this->y = 0.0f;
}

void Vector2::Init(float value) {
	this->x = value;
	this->y = value;
}

Vector2 Engine::Vector2::AnyInit(float value) {
	return { value, value };
}

float Vector2::Length(const Vector2& v) {
	return std::sqrtf(v.x * v.x + v.y * v.y);
}

float Vector2::Length() const {
	return std::sqrtf(x * x + y * y);
}

Vector2 Vector2::Normalize(const Vector2& v) {
	float length = Length(v);
	if (length <= 0.001f) {
		return Vector2(0.0f, 0.0f);
	}
	return Vector2(v.x / length, v.y / length);
}

Vector2 Vector2::Normalize() const {
	float length = this->Length();
	if (length <= 0.001f) {
		return Vector2(0.0f, 0.0f);
	}
	return Vector2(x / length, y / length);
}

float Vector2::Dot(const Vector2& v0, const Vector2& v1) {
	return v0.x * v1.x + v0.y * v1.y;
}

Vector2 Vector2::Cross(const Vector2& v0, const Vector2& v1) {
	float cross = v0.x * v1.y - v0.y * v1.x;
	return Vector2(cross, 0.0f);
}

Vector2 Vector2::Lerp(const Vector2& v0, const Vector2& v1, float lerpT) {
	return Vector2(std::lerp(v0.x, v1.x, lerpT), std::lerp(v0.y, v1.y, lerpT));
}

Vector2 Vector2::Lerp(const Vector2& v0, const Vector2& v1, const Vector2& lerpT) {
	return Vector2(std::lerp(v0.x, v1.x, lerpT.x), std::lerp(v0.y, v1.y, lerpT.y));
}

//============================================================================
//	Vector2I structMethods
//============================================================================

Vector2I Vector2I::operator+(const Vector2I& other) const {
	return Vector2I(x + other.x, y + other.y);
}
Vector2I Vector2I::operator-(const Vector2I& other) const {
	return Vector2I(x - other.x, y - other.y);
}
Vector2I Vector2I::operator*(const Vector2I& other) const {
	return Vector2I(x * other.x, y * other.y);
}
Vector2I Vector2I::operator/(const Vector2I& other) const {
	return Vector2I(x / other.x, y / other.y);
}

Vector2I& Vector2I::operator+=(const Vector2I& v) {
	x += v.x;
	y += v.y;
	return *this;
}
Vector2I& Vector2I::operator-=(const Vector2I& v) {
	x -= v.x;
	y -= v.y;
	return *this;
}
Vector2I& Vector2I::operator*=(const Vector2I& v) {
	x *= v.x;
	y *= v.y;
	return *this;
}
Vector2I& Vector2I::operator/=(const Vector2I& v) {
	x /= v.x;
	y /= v.y;
	return *this;
}

Vector2I Vector2I::operator+(int32_t scalar) const {
	return Vector2I(x + scalar, y + scalar);
}
Vector2I Vector2I::operator-(int32_t scalar) const {
	return Vector2I(x - scalar, y - scalar);
}
Vector2I Vector2I::operator*(int32_t scalar) const {
	return Vector2I(x * scalar, y * scalar);
}
Vector2I Vector2I::operator/(int32_t scalar) const {
	return Vector2I(x / scalar, y / scalar);
}

Vector2I& Vector2I::operator+=(int32_t scalar) {
	x += scalar;
	y += scalar;
	return *this;
}
Vector2I& Vector2I::operator-=(int32_t scalar) {
	x -= scalar;
	y -= scalar;
	return *this;
}
Vector2I& Vector2I::operator*=(int32_t scalar) {
	x *= scalar;
	y *= scalar;
	return *this;
}
Vector2I& Vector2I::operator/=(int32_t scalar) {
	x /= scalar;
	y /= scalar;
	return *this;
}

Vector2I Vector2I::operator-() const {
	return Vector2I(-x, -y);
}

bool Vector2I::operator==(const Vector2I& other) const {
	return x == other.x && y == other.y;
}
bool Vector2I::operator!=(const Vector2I& other) const {
	return !(*this == other);
}
bool Vector2I::operator>=(const Vector2I& other) const {
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y)) >=
		std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) + static_cast<double>(other.y) * static_cast<double>(other.y));
}
bool Vector2I::operator<=(const Vector2I& other) const {
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y)) <=
		std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) + static_cast<double>(other.y) * static_cast<double>(other.y));
}

nlohmann::json Vector2I::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y} };
}
Vector2I Vector2I::FromJson(const nlohmann::json& data) {
	Vector2I v{};
	if (data.is_array() && data.size() == 2) {
		v.x = data[0].get<int32_t>();
		v.y = data[1].get<int32_t>();
	} else if (data.contains("x") && data.contains("y")) {
		v.x = data["x"].get<int32_t>();
		v.y = data["y"].get<int32_t>();
	}
	return v;
}

void Vector2I::Init() {
	this->x = 0;
	this->y = 0;
}

void Vector2I::Init(int32_t value) {
	this->x = value;
	this->y = value;
}

std::vector<uint32_t> Vector2I::ToUInt() const {
	return { static_cast<uint32_t>(x), static_cast<uint32_t>(y) };
}