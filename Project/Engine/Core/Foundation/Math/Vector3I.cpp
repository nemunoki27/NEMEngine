#include "Vector3.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

using namespace Engine;

//============================================================================
//	Vector3I structMethods
//============================================================================
Vector3I Vector3I::operator+(const Vector3I& other) const {

	// 対応する成分を加算
	return Vector3I(x + other.x, y + other.y, z + other.z);
}

Vector3I Vector3I::operator-(const Vector3I& other) const {

	// 対応する成分を減算
	return Vector3I(x - other.x, y - other.y, z - other.z);
}

Vector3I Vector3I::operator*(const Vector3I& other) const {

	// 各成分の積を計算
	return Vector3I(x * other.x, y * other.y, z * other.z);
}

Vector3I Vector3I::operator/(const Vector3I& other) const {

	// 各成分を除算
	return Vector3I(x / other.x, y / other.y, z / other.z);
}

Vector3I& Vector3I::operator+=(const Vector3I& v) {

	// 対応する成分を加算
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}

Vector3I& Vector3I::operator-=(const Vector3I& v) {

	// 対応する成分を減算
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}

Vector3I& Vector3I::operator*=(const Vector3I& v) {

	// 各成分の積を計算
	x *= v.x;
	y *= v.y;
	z *= v.z;
	return *this;
}

Vector3I& Vector3I::operator/=(const Vector3I& v) {

	// 各成分を除算
	x /= v.x;
	y /= v.y;
	z /= v.z;
	return *this;
}

Vector3I Vector3I::operator+(int32_t scalar) const {

	// 対応する成分を加算
	return Vector3I(x + scalar, y + scalar, z + scalar);
}

Vector3I Vector3I::operator-(int32_t scalar) const {

	// 対応する成分を減算
	return Vector3I(x - scalar, y - scalar, z - scalar);
}

Vector3I Vector3I::operator*(int32_t scalar) const {

	// 各成分の積を計算
	return Vector3I(x * scalar, y * scalar, z * scalar);
}

Vector3I Vector3I::operator/(int32_t scalar) const {

	// 各成分を除算
	return Vector3I(x / scalar, y / scalar, z / scalar);
}

Vector3I& Vector3I::operator+=(int32_t scalar) {

	// 対応する成分を加算
	x += scalar;
	y += scalar;
	z += scalar;
	return *this;
}

Vector3I& Vector3I::operator-=(int32_t scalar) {

	// 対応する成分を減算
	x -= scalar;
	y -= scalar;
	z -= scalar;
	return *this;
}

Vector3I& Vector3I::operator*=(int32_t scalar) {

	// 各成分の積を計算
	x *= scalar;
	y *= scalar;
	z *= scalar;
	return *this;
}

Vector3I& Vector3I::operator/=(int32_t scalar) {

	// 各成分を除算
	x /= scalar;
	y /= scalar;
	z /= scalar;
	return *this;
}

Vector3I Vector3I::operator-() const {

	// 全成分の符号を反転
	return Vector3I(-x, -y, -z);
}

bool Vector3I::operator==(const Vector3I& other) const {

	// 全成分を比較
	return x == other.x && y == other.y && z == other.z;
}

bool Vector3I::operator!=(const Vector3I& other) const {

	// 全成分を比較
	return !(*this == other);
}

bool Vector3I::operator>=(const Vector3I& other) const {

	// 両方の長さを比較
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y) +
					 static_cast<double>(z) * static_cast<double>(z)) >=
		   std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) +
					 static_cast<double>(other.y) * static_cast<double>(other.y) +
					 static_cast<double>(other.z) * static_cast<double>(other.z));
}

bool Vector3I::operator<=(const Vector3I& other) const {

	// 両方の長さを比較
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y) +
					 static_cast<double>(z) * static_cast<double>(z)) <=
		   std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) +
					 static_cast<double>(other.y) * static_cast<double>(other.y) +
					 static_cast<double>(other.z) * static_cast<double>(other.z));
}

void Vector3I::Init() {

	// 各成分を初期化
	this->x = 0;
	this->y = 0;
	this->z = 0;
}

void Vector3I::Init(int32_t value) {

	// 各成分を初期化
	this->x = value;
	this->y = value;
	this->z = value;
}

std::vector<uint32_t> Vector3I::ToUInt() const {

	// 各成分を符号なし整数へ変換
	return {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)};
}
