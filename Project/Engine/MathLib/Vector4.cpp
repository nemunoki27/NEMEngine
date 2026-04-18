#include "Vector4.h"

using namespace Engine;

//============================================================================
//	Vector4 structMethods
//============================================================================

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
Vector4& Vector4::operator*=(const Vector4& v) {
	x *= v.x;
	y *= v.y;
	z *= v.z;
	w *= v.w;
	return *this;
}
Vector4& Vector4::operator/=(const Vector4& v) {
	x /= v.x;
	y /= v.y;
	z /= v.z;
	w /= v.w;
	return *this;
}

Vector4 Vector4::operator+(float scalar) const {
	return Vector4(x + scalar, y + scalar, z + scalar, w + scalar);
}
Vector4 Vector4::operator-(float scalar) const {
	return Vector4(x - scalar, y - scalar, z - scalar, w - scalar);
}
Vector4 Vector4::operator*(float scalar) const {
	return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
}
Vector4 Vector4::operator/(float scalar) const {
	return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
}

Vector4& Vector4::operator+=(float scalar) {
	x += scalar;
	y += scalar;
	z += scalar;
	w += scalar;
	return *this;
}
Vector4& Vector4::operator-=(float scalar) {
	x -= scalar;
	y -= scalar;
	z -= scalar;
	w -= scalar;
	return *this;
}
Vector4& Vector4::operator*=(float scalar) {
	x *= scalar;
	y *= scalar;
	z *= scalar;
	w *= scalar;
	return *this;
}
Vector4& Vector4::operator/=(float scalar) {
	x /= scalar;
	y /= scalar;
	z /= scalar;
	w /= scalar;
	return *this;
}

Vector4 Vector4::operator-() const {
	return Vector4(-x, -y, -z, -w);
}

bool Vector4::operator==(const Vector4& other) const {
	return x == other.x && y == other.y && z == other.z && w == other.w;
}
bool Vector4::operator!=(const Vector4& other) const {
	return !(*this == other);
}
bool Vector4::operator>=(const Vector4& other) const {
	return std::sqrtf(x * x + y * y + z * z + w * w) >= std::sqrtf(other.x * other.x + other.y * other.y + other.z * other.z + other.w * other.w);
}
bool Vector4::operator<=(const Vector4& other) const {
	return std::sqrtf(x * x + y * y + z * z + w * w) <= std::sqrtf(other.x * other.x + other.y * other.y + other.z * other.z + other.w * other.w);
}

nlohmann::json Vector4::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y}, {"z", z}, {"w", w} };
}
Vector4 Vector4::FromJson(const nlohmann::json& data) {
	Vector4 v{};
	if (data.is_array() && data.size() == 4) {
		v.x = data[0].get<float>();
		v.y = data[1].get<float>();
		v.z = data[2].get<float>();
		v.w = data[3].get<float>();
	} else if (data.contains("x") && data.contains("y") &&
		data.contains("z") && data.contains("w")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
		v.z = data["z"].get<float>();
		v.w = data["w"].get<float>();
	}
	return v;
}

void Vector4::Init() {
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
	this->w = 0.0f;
}