#include "Vector4.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

using namespace Engine;

//============================================================================
//	Vector4 structMethods
//============================================================================
Vector4 Vector4::operator+(const Vector4& other) const {

	// 対応する成分を加算
	return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
}

Vector4 Vector4::operator-(const Vector4& other) const {

	// 対応する成分を減算
	return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
}

Vector4 Vector4::operator*(const Vector4& other) const {

	// 各成分の積を計算
	return Vector4(x * other.x, y * other.y, z * other.z, w * other.w);
}

Vector4 Vector4::operator/(const Vector4& other) const {

	// 各成分を除算
	return Vector4(x / other.x, y / other.y, z / other.z, w / other.w);
}

Vector4& Vector4::operator+=(const Vector4& v) {

	// 対応する成分を加算
	x += v.x;
	y += v.y;
	z += v.z;
	w += v.w;
	return *this;
}

Vector4& Vector4::operator-=(const Vector4& v) {

	// 対応する成分を減算
	x -= v.x;
	y -= v.y;
	z -= v.z;
	w -= v.w;
	return *this;
}

Vector4& Vector4::operator*=(const Vector4& v) {

	// 各成分の積を計算
	x *= v.x;
	y *= v.y;
	z *= v.z;
	w *= v.w;
	return *this;
}

Vector4& Vector4::operator/=(const Vector4& v) {

	// 各成分を除算
	x /= v.x;
	y /= v.y;
	z /= v.z;
	w /= v.w;
	return *this;
}

Vector4 Vector4::operator+(float scalar) const {

	// 対応する成分を加算
	return Vector4(x + scalar, y + scalar, z + scalar, w + scalar);
}

Vector4 Vector4::operator-(float scalar) const {

	// 対応する成分を減算
	return Vector4(x - scalar, y - scalar, z - scalar, w - scalar);
}

Vector4 Vector4::operator*(float scalar) const {

	// 各成分の積を計算
	return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
}

Vector4 Vector4::operator/(float scalar) const {

	// 各成分を除算
	return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
}

Vector4& Vector4::operator+=(float scalar) {

	// 対応する成分を加算
	x += scalar;
	y += scalar;
	z += scalar;
	w += scalar;
	return *this;
}

Vector4& Vector4::operator-=(float scalar) {

	// 対応する成分を減算
	x -= scalar;
	y -= scalar;
	z -= scalar;
	w -= scalar;
	return *this;
}

Vector4& Vector4::operator*=(float scalar) {

	// 各成分の積を計算
	x *= scalar;
	y *= scalar;
	z *= scalar;
	w *= scalar;
	return *this;
}

Vector4& Vector4::operator/=(float scalar) {

	// 各成分を除算
	x /= scalar;
	y /= scalar;
	z /= scalar;
	w /= scalar;
	return *this;
}

Vector4 Vector4::operator-() const {

	// 全成分の符号を反転
	return Vector4(-x, -y, -z, -w);
}

bool Vector4::operator==(const Vector4& other) const {

	// 全成分を比較
	return x == other.x && y == other.y && z == other.z && w == other.w;
}

bool Vector4::operator!=(const Vector4& other) const {

	// 全成分を比較
	return !(*this == other);
}

bool Vector4::operator>=(const Vector4& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y, z, w) >= Math::SquaredLength(other.x, other.y, other.z, other.w);
}

bool Vector4::operator<=(const Vector4& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y, z, w) <= Math::SquaredLength(other.x, other.y, other.z, other.w);
}

void Vector4::Init() {

	// 各成分を初期化
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
	this->w = 0.0f;
}
