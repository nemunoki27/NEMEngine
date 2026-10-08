#include "Vector3.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>

using namespace Engine;

//============================================================================
//	Vector3 structMethods
//============================================================================
Vector3 Vector3::operator+(const Vector3& other) const {

	// 対応する成分を加算
	return Vector3(x + other.x, y + other.y, z + other.z);
}

Vector3 Vector3::operator-(const Vector3& other) const {

	// 対応する成分を減算
	return Vector3(x - other.x, y - other.y, z - other.z);
}

Vector3 Vector3::operator*(const Vector3& other) const {

	// 各成分の積を計算
	return Vector3(x * other.x, y * other.y, z * other.z);
}

Vector3 Vector3::operator/(const Vector3& other) const {

	// 各成分を除算
	return Vector3(x / other.x, y / other.y, z / other.z);
}

Vector3& Vector3::operator+=(const Vector3& v) {

	// 対応する成分を加算
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}

Vector3& Vector3::operator-=(const Vector3& v) {

	// 対応する成分を減算
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}

Vector3& Vector3::operator*=(const Vector3& v) {

	// 各成分の積を計算
	x *= v.x;
	y *= v.y;
	z *= v.z;
	return *this;
}

Vector3& Vector3::operator/=(const Vector3& v) {

	// 各成分を除算
	x /= v.x;
	y /= v.y;
	z /= v.z;
	return *this;
}

Vector3 Vector3::operator+(float scalar) const {

	// 対応する成分を加算
	return Vector3(x + scalar, y + scalar, z + scalar);
}

Vector3 Vector3::operator-(float scalar) const {

	// 対応する成分を減算
	return Vector3(x - scalar, y - scalar, z - scalar);
}

Vector3 Vector3::operator*(float scalar) const {

	// 各成分の積を計算
	return Vector3(x * scalar, y * scalar, z * scalar);
}

Vector3 Vector3::operator/(float scalar) const {

	// 各成分を除算
	return Vector3(x / scalar, y / scalar, z / scalar);
}

Vector3& Vector3::operator+=(float scalar) {

	// 対応する成分を加算
	x += scalar;
	y += scalar;
	z += scalar;
	return *this;
}

Vector3& Vector3::operator-=(float scalar) {

	// 対応する成分を減算
	x -= scalar;
	y -= scalar;
	z -= scalar;
	return *this;
}

Vector3& Vector3::operator*=(float scalar) {

	// 各成分の積を計算
	x *= scalar;
	y *= scalar;
	z *= scalar;
	return *this;
}

Vector3& Vector3::operator/=(float scalar) {

	// 各成分を除算
	x /= scalar;
	y /= scalar;
	z /= scalar;
	return *this;
}

Vector3 Vector3::operator-() const {

	// 全成分の符号を反転
	return Vector3(-x, -y, -z);
}

bool Vector3::operator==(const Vector3& other) const {

	// 全成分を比較
	return x == other.x && y == other.y && z == other.z;
}

bool Vector3::operator!=(const Vector3& other) const {

	// 全成分を比較
	return !(*this == other);
}

bool Vector3::operator>=(const Vector3& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y, z) >= Math::SquaredLength(other.x, other.y, other.z);
}

bool Vector3::operator<=(const Vector3& other) const {

	// 両方の長さを比較
	return Math::SquaredLength(x, y, z) <= Math::SquaredLength(other.x, other.y, other.z);
}

namespace Engine {
	Vector3 operator+(float scalar, const Vector3& v) {

		// 各成分にスカラーを加算
		return Vector3(scalar + v.x, scalar + v.y, scalar + v.z);
	}
	Vector3 operator-(float scalar, const Vector3& v) {

		// 各成分をスカラーから減算
		return Vector3(scalar - v.x, scalar - v.y, scalar - v.z);
	}
	Vector3 operator*(float scalar, const Vector3& v) {

		// 各成分へスカラーを乗算
		return Vector3(scalar * v.x, scalar * v.y, scalar * v.z);
	}
	Vector3 operator/(float scalar, const Vector3& v) {

		// スカラーを各成分で除算
		return Vector3(scalar / v.x, scalar / v.y, scalar / v.z);
	}
}

void Vector3::Init() {

	// 各成分を初期化
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
}

void Vector3::Init(float value) {

	// 各成分を初期化
	this->x = value;
	this->y = value;
	this->z = value;
}

Vector3 Vector3::AnyInit(float value) {

	// 全成分を同じ値で作成
	return {value, value, value};
}

float Vector3::Length(const Vector3& v) {

	// 各成分から長さを計算
	return static_cast<float>(std::sqrt(Math::SquaredLength(v.x, v.y, v.z)));
}

float Vector3::Length() const {

	// 各成分から長さを計算
	return Length(*this);
}

Vector3 Vector3::Normalize(const Vector3& v) {

	// 長さを揃えて正規化
	return NormalizeOr(v, {}, 0.001f);
}

Vector3 Vector3::Normalize() const {

	// 長さを揃えて正規化
	return Normalize(*this);
}

Vector3 Vector3::NormalizeOr(const Vector3& value, const Vector3& fallback, float epsilon) {

	// 長さを確認して正規化
	const double length = std::hypot(static_cast<double>(value.x), value.y, value.z);
	if (!std::isfinite(length) || length == 0.0 || length <= epsilon) {
		return fallback;
	}
	return Vector3(
		static_cast<float>(value.x / length), static_cast<float>(value.y / length), static_cast<float>(value.z / length));
}

float Vector3::Dot(const Vector3& v0, const Vector3& v1) {

	// 対応する成分の積を加算
	return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
}

Vector3 Vector3::Cross(const Vector3& v0, const Vector3& v1) {

	// 外積から直交方向を計算
	return {v0.y * v1.z - v0.z * v1.y, v0.z * v1.x - v0.x * v1.z, v0.x * v1.y - v0.y * v1.x};
}

Vector3 Vector3::Lerp(const Vector3& v0, const Vector3& v1, float lerpT) {

	// 両端の値を補間
	return Vector3(std::lerp(v0.x, v1.x, lerpT), std::lerp(v0.y, v1.y, lerpT), std::lerp(v0.z, v1.z, lerpT));
}

Vector3 Vector3::Lerp(const Vector3& v0, const Vector3& v1, const Vector3& lerpT) {

	// 両端の値を補間
	return Vector3(std::lerp(v0.x, v1.x, lerpT.x), std::lerp(v0.y, v1.y, lerpT.y), std::lerp(v0.z, v1.z, lerpT.z));
}

Vector3 Vector3::Reflect(const Vector3& input, const Vector3& normal) {

	// 法線に対する反射方向を計算
	return input - normal * (2.0f * Dot(input, normal));
}

Vector3 Vector3::Transform(const Vector3& v, const Matrix4x4& matrix) {

	// 行ベクトル規約で座標を変換
	Vector3 result;
	result.x = v.x * matrix.m[0][0] + v.y * matrix.m[1][0] + v.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = v.x * matrix.m[0][1] + v.y * matrix.m[1][1] + v.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = v.x * matrix.m[0][2] + v.y * matrix.m[1][2] + v.z * matrix.m[2][2] + matrix.m[3][2];
	float w = v.x * matrix.m[0][3] + v.y * matrix.m[1][3] + v.z * matrix.m[2][3] + matrix.m[3][3];
	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}
	return result;
}

Vector3 Engine::Vector3::TransferNormal(const Vector3& v, const Matrix4x4& m) {

	// 平行移動を除いて方向を変換
	Vector3 vector{};
	vector.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
	vector.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
	vector.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
	return vector;
}

Vector3 Vector3::Projection(const Vector3& v0, const Vector3& v1) {

	// 指定方向への射影を計算
	Vector3 vector{};
	Vector3 normalizedV1 = Normalize(v1);
	vector = normalizedV1 * Dot(v0, normalizedV1);
	return vector;
}

bool Engine::Vector3::NearlyEqual(const Vector3& v0, const Vector3& v1) {

	// 許容誤差の範囲で比較
	return Math::NearlyEqual(v0.x, v1.x) && Math::NearlyEqual(v0.y, v1.y) && Math::NearlyEqual(v0.z, v1.z);
}

Vector3 Engine::Vector3::MakeContinuousDegrees(const Vector3& rawEuler, const Vector3& referenceEuler) {

	// 参照角度に近い周期へ揃える
	return {Math::MakeContinuousAngleDegrees(rawEuler.x, referenceEuler.x),
		Math::MakeContinuousAngleDegrees(rawEuler.y, referenceEuler.y),
		Math::MakeContinuousAngleDegrees(rawEuler.z, referenceEuler.z)};
}
