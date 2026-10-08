#include "Vector2.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>

using namespace Engine;

//============================================================================
//	Vector2 structMethods
//============================================================================
Vector2 Vector2::operator+(const Vector2& other) const {

	// 対応する成分を加算
	return Vector2(x + other.x, y + other.y);
}

Vector2 Vector2::operator-(const Vector2& other) const {

	// 対応する成分を減算
	return Vector2(x - other.x, y - other.y);
}

Vector2 Vector2::operator*(const Vector2& other) const {

	// 各成分の積を計算
	return Vector2(x * other.x, y * other.y);
}

Vector2 Vector2::operator/(const Vector2& other) const {

	// 各成分を除算
	return Vector2(x / other.x, y / other.y);
}

Vector2& Vector2::operator+=(const Vector2& v) {

	// 対応する成分を加算
	x += v.x;
	y += v.y;
	return *this;
}

Vector2& Vector2::operator-=(const Vector2& v) {

	// 対応する成分を減算
	x -= v.x;
	y -= v.y;
	return *this;
}

Vector2& Vector2::operator*=(const Vector2& v) {

	// 各成分の積を計算
	x *= v.x;
	y *= v.y;
	return *this;
}

Vector2& Vector2::operator/=(const Vector2& v) {

	// 各成分を除算
	x /= v.x;
	y /= v.y;
	return *this;
}

Vector2 Vector2::operator+(float scalar) const {

	// 対応する成分を加算
	return Vector2(x + scalar, y + scalar);
}

Vector2 Vector2::operator-(float scalar) const {

	// 対応する成分を減算
	return Vector2(x - scalar, y - scalar);
}

Vector2 Vector2::operator*(float scalar) const {

	// 各成分の積を計算
	return Vector2(x * scalar, y * scalar);
}

Vector2 Vector2::operator/(float scalar) const {

	// 各成分を除算
	return Vector2(x / scalar, y / scalar);
}

Vector2& Vector2::operator+=(float scalar) {

	// 対応する成分を加算
	x += scalar;
	y += scalar;
	return *this;
}

Vector2& Vector2::operator-=(float scalar) {

	// 対応する成分を減算
	x -= scalar;
	y -= scalar;
	return *this;
}

Vector2& Vector2::operator*=(float scalar) {

	// 各成分の積を計算
	x *= scalar;
	y *= scalar;
	return *this;
}

Vector2& Vector2::operator/=(float scalar) {

	// 各成分を除算
	x /= scalar;
	y /= scalar;
	return *this;
}

Vector2 Vector2::operator-() const {

	// 全成分の符号を反転
	return Vector2(-x, -y);
}

bool Vector2::operator==(const Vector2& other) const {

	// 全成分を比較
	return x == other.x && y == other.y;
}

bool Vector2::operator!=(const Vector2& other) const {

	// 全成分を比較
	return !(*this == other);
}

bool Vector2::operator>=(const Vector2& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y) >= Math::SquaredLength(other.x, other.y);
}

bool Vector2::operator<=(const Vector2& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y) <= Math::SquaredLength(other.x, other.y);
}

void Vector2::Init() {

	// 各成分を初期化
	this->x = 0.0f;
	this->y = 0.0f;
}

void Vector2::Init(float value) {

	// 各成分を初期化
	this->x = value;
	this->y = value;
}

Vector2 Engine::Vector2::AnyInit(float value) {

	// 全成分を同じ値で作成
	return {value, value};
}

float Vector2::Length(const Vector2& v) {

	// 各成分から長さを計算
	return static_cast<float>(std::sqrt(Math::SquaredLength(v.x, v.y)));
}

float Vector2::Length() const {

	// 各成分から長さを計算
	return Length(*this);
}

Vector2 Vector2::Normalize(const Vector2& v) {

	// 長さを揃えて正規化
	const double length = std::hypot(static_cast<double>(v.x), v.y);
	if (!std::isfinite(length) || length <= 0.001f) {
		return Vector2(0.0f, 0.0f);
	}
	return Vector2(static_cast<float>(v.x / length), static_cast<float>(v.y / length));
}

Vector2 Vector2::Normalize() const {

	// 長さを揃えて正規化
	return Normalize(*this);
}

float Vector2::Dot(const Vector2& v0, const Vector2& v1) {

	// 対応する成分の積を加算
	return v0.x * v1.x + v0.y * v1.y;
}

Vector2 Vector2::Cross(const Vector2& v0, const Vector2& v1) {

	// 符号付き外積をX成分へ格納
	float cross = v0.x * v1.y - v0.y * v1.x;
	return Vector2(cross, 0.0f);
}

Vector2 Vector2::Lerp(const Vector2& v0, const Vector2& v1, float lerpT) {

	// 両端の値を補間
	return Vector2(std::lerp(v0.x, v1.x, lerpT), std::lerp(v0.y, v1.y, lerpT));
}

Vector2 Vector2::Lerp(const Vector2& v0, const Vector2& v1, const Vector2& lerpT) {

	// 両端の値を補間
	return Vector2(std::lerp(v0.x, v1.x, lerpT.x), std::lerp(v0.y, v1.y, lerpT.y));
}
