#include "Vector2.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

using namespace Engine;

//============================================================================
//	Vector2I structMethods
//============================================================================
Vector2I Vector2I::operator+(const Vector2I& other) const {

	// 対応する成分を加算
	return Vector2I(x + other.x, y + other.y);
}

Vector2I Vector2I::operator-(const Vector2I& other) const {

	// 対応する成分を減算
	return Vector2I(x - other.x, y - other.y);
}

Vector2I Vector2I::operator*(const Vector2I& other) const {

	// 各成分の積を計算
	return Vector2I(x * other.x, y * other.y);
}

Vector2I Vector2I::operator/(const Vector2I& other) const {

	// 各成分を除算
	return Vector2I(x / other.x, y / other.y);
}

Vector2I& Vector2I::operator+=(const Vector2I& v) {

	// 対応する成分を加算
	x += v.x;
	y += v.y;
	return *this;
}

Vector2I& Vector2I::operator-=(const Vector2I& v) {

	// 対応する成分を減算
	x -= v.x;
	y -= v.y;
	return *this;
}

Vector2I& Vector2I::operator*=(const Vector2I& v) {

	// 各成分の積を計算
	x *= v.x;
	y *= v.y;
	return *this;
}

Vector2I& Vector2I::operator/=(const Vector2I& v) {

	// 各成分を除算
	x /= v.x;
	y /= v.y;
	return *this;
}

Vector2I Vector2I::operator+(int32_t scalar) const {

	// 対応する成分を加算
	return Vector2I(x + scalar, y + scalar);
}

Vector2I Vector2I::operator-(int32_t scalar) const {

	// 対応する成分を減算
	return Vector2I(x - scalar, y - scalar);
}

Vector2I Vector2I::operator*(int32_t scalar) const {

	// 各成分の積を計算
	return Vector2I(x * scalar, y * scalar);
}

Vector2I Vector2I::operator/(int32_t scalar) const {

	// 各成分を除算
	return Vector2I(x / scalar, y / scalar);
}

Vector2I& Vector2I::operator+=(int32_t scalar) {

	// 対応する成分を加算
	x += scalar;
	y += scalar;
	return *this;
}

Vector2I& Vector2I::operator-=(int32_t scalar) {

	// 対応する成分を減算
	x -= scalar;
	y -= scalar;
	return *this;
}

Vector2I& Vector2I::operator*=(int32_t scalar) {

	// 各成分の積を計算
	x *= scalar;
	y *= scalar;
	return *this;
}

Vector2I& Vector2I::operator/=(int32_t scalar) {

	// 各成分を除算
	x /= scalar;
	y /= scalar;
	return *this;
}

Vector2I Vector2I::operator-() const {

	// 全成分の符号を反転
	return Vector2I(-x, -y);
}

bool Vector2I::operator==(const Vector2I& other) const {

	// 全成分を比較
	return x == other.x && y == other.y;
}

bool Vector2I::operator!=(const Vector2I& other) const {

	// 全成分を比較
	return !(*this == other);
}

bool Vector2I::operator>=(const Vector2I& other) const {

	// 両方の長さを比較
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y)) >=
		   std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) +
					 static_cast<double>(other.y) * static_cast<double>(other.y));
}

bool Vector2I::operator<=(const Vector2I& other) const {

	// 両方の長さを比較
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y)) <=
		   std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) +
					 static_cast<double>(other.y) * static_cast<double>(other.y));
}

void Vector2I::Init() {

	// 各成分を初期化
	this->x = 0;
	this->y = 0;
}

void Vector2I::Init(int32_t value) {

	// 各成分を初期化
	this->x = value;
	this->y = value;
}

Vector2 Engine::Vector2I::GetFloat() const {

	// 整数成分を浮動小数点へ変換
	return {static_cast<float>(this->x), static_cast<float>(this->y)};
}

std::vector<uint32_t> Vector2I::ToUInt() const {

	// 各成分を符号なし整数へ変換
	return {static_cast<uint32_t>(this->x), static_cast<uint32_t>(this->y)};
}