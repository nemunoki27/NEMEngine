#include "Vector3.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

//============================================================================
//	Vector3 structMethods
//============================================================================

Vector3 Vector3::operator+(const Vector3& other) const {
	return Vector3(x + other.x, y + other.y, z + other.z);
}
Vector3 Vector3::operator-(const Vector3& other) const {
	return Vector3(x - other.x, y - other.y, z - other.z);
}
Vector3 Vector3::operator*(const Vector3& other) const {
	return Vector3(x * other.x, y * other.y, z * other.z);
}
Vector3 Vector3::operator/(const Vector3& other) const {
	return Vector3(x / other.x, y / other.y, z / other.z);
}

Vector3& Vector3::operator+=(const Vector3& v) {
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}
Vector3& Vector3::operator-=(const Vector3& v) {
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}
Vector3& Vector3::operator*=(const Vector3& v) {
	x *= v.x;
	y *= v.y;
	z *= v.z;
	return *this;
}
Vector3& Vector3::operator/=(const Vector3& v) {
	x /= v.x;
	y /= v.y;
	z /= v.z;
	return *this;
}

Vector3 Vector3::operator+(float scalar) const {
	return Vector3(x + scalar, y + scalar, z + scalar);
}
Vector3 Vector3::operator-(float scalar) const {
	return Vector3(x - scalar, y - scalar, z - scalar);
}
Vector3 Vector3::operator*(float scalar) const {
	return Vector3(x * scalar, y * scalar, z * scalar);
}
Vector3 Vector3::operator/(float scalar) const {
	return Vector3(x / scalar, y / scalar, z / scalar);
}

Vector3& Vector3::operator+=(float scalar) {
	x += scalar;
	y += scalar;
	z += scalar;
	return *this;
}
Vector3& Vector3::operator-=(float scalar) {
	x -= scalar;
	y -= scalar;
	z -= scalar;
	return *this;
}
Vector3& Vector3::operator*=(float scalar) {
	x *= scalar;
	y *= scalar;
	z *= scalar;
	return *this;
}
Vector3& Vector3::operator/=(float scalar) {
	x /= scalar;
	y /= scalar;
	z /= scalar;
	return *this;
}

Vector3 Vector3::operator-() const {
	return Vector3(-x, -y, -z);
}

bool Vector3::operator==(const Vector3& other) const {
	return x == other.x && y == other.y && z == other.z;
}
bool Vector3::operator!=(const Vector3& other) const {
	return !(*this == other);
}
bool Vector3::operator>=(const Vector3& other) const {
	return this->Length() >= other.Length();
}
bool Vector3::operator<=(const Vector3& other) const {
	return this->Length() <= other.Length();
}
namespace Engine {
	Vector3 operator+(float scalar, const Vector3& v) {
		return Vector3(scalar + v.x, scalar + v.y, scalar + v.z);
	}
	Vector3 operator-(float scalar, const Vector3& v) {
		return Vector3(scalar - v.x, scalar - v.y, scalar - v.z);
	}
	Vector3 operator*(float scalar, const Vector3& v) {
		return Vector3(scalar * v.x, scalar * v.y, scalar * v.z);
	}
	Vector3 operator/(float scalar, const Vector3& v) {
		return Vector3(scalar / v.x, scalar / v.y, scalar / v.z);
	}
}

nlohmann::json Vector3::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y}, {"z", z} };
}
Vector3 Vector3::FromJson(const nlohmann::json& data) {
	Vector3 v{};
	if (data.is_array() && data.size() == 3) {
		v.x = data[0].get<float>();
		v.y = data[2].get<float>();
		v.z = data[1].get<float>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z")) {
		v.x = data["x"].get<float>();
		v.y = data["y"].get<float>();
		v.z = data["z"].get<float>();
	}
	return v;
}

void Vector3::Init() {
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
}

void Vector3::Init(float value) {
	this->x = value;
	this->y = value;
	this->z = value;
}

Vector3 Vector3::AnyInit(float value) {
	return { value ,value ,value };
}

float Vector3::Length(const Vector3& v) {
	return std::sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

float Vector3::Length() const {
	return std::sqrtf(x * x + y * y + z * z);
}

Vector3 Vector3::Normalize(const Vector3& v) {
	float length = Length(v);
	if (length <= 0.001f) {
		return Vector3(0.0f, 0.0f, 0.0f);
	}
	return Vector3(v.x / length, v.y / length, v.z / length);
}

Vector3 Vector3::Normalize() const {
	float length = this->Length();
	if (length <= 0.001f) {
		return Vector3(0.0f, 0.0f, 0.0f);
	}
	return Vector3(x / length, y / length, z / length);
}

float Vector3::Dot(const Vector3& v0, const Vector3& v1) {
	return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
}

Vector3 Vector3::Cross(const Vector3& v0, const Vector3& v1) {
	return { v0.y * v1.z - v0.z * v1.y,v0.z * v1.x - v0.x * v1.z,v0.x * v1.y - v0.y * v1.x };
}

Vector3 Vector3::Lerp(const Vector3& v0, const Vector3& v1, float lerpT) {
	return Vector3(std::lerp(v0.x, v1.x, lerpT), std::lerp(v0.y, v1.y, lerpT), std::lerp(v0.z, v1.z, lerpT));
}

Vector3 Vector3::Lerp(const Vector3& v0, const Vector3& v1, const Vector3& lerpT) {
	return Vector3(std::lerp(v0.x, v1.x, lerpT.x), std::lerp(v0.y, v1.y, lerpT.y), std::lerp(v0.z, v1.z, lerpT.z));
}

Vector3 Vector3::Reflect(const Vector3& input, const Vector3& normal) {
	return input - normal * (2.0f * Dot(input, normal));
}

Vector3 Vector3::Transform(const Vector3& v, const Matrix4x4& matrix) {
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
	Vector3 vector{};
	vector.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
	vector.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
	vector.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
	return vector;
}

Vector3 Engine::Vector3::TransformPoint(const Vector3& v, const Matrix4x4& m) {

	// 4次元ベクトル (x, y, z, w) を作成
	float x = v.x * m.m[0][0] + v.y * m.m[0][1] + v.z * m.m[0][2] + m.m[0][3];
	float y = v.x * m.m[1][0] + v.y * m.m[1][1] + v.z * m.m[1][2] + m.m[1][3];
	float z = v.x * m.m[2][0] + v.y * m.m[2][1] + v.z * m.m[2][2] + m.m[2][3];
	float w = v.x * m.m[3][0] + v.y * m.m[3][1] + v.z * m.m[3][2] + m.m[3][3];

	// wで正規化して3D座標を返す
	if (w != 0.0f) {
		x /= w;
		y /= w;
		z /= w;
	}
	return Vector3(x, y, z);
}

Vector3 Vector3::Projection(const Vector3& v0, const Vector3& v1) {
	Vector3 vector{};
	Vector3 normalizedV1 = Normalize(v1);
	vector = normalizedV1 * Dot(v0, normalizedV1);
	return vector;
}

bool Engine::Vector3::NearlyEqual(const Vector3& v0, const Vector3& v1) {
	return Math::NearlyEqual(v0.x, v1.x) && Math::NearlyEqual(v0.y, v1.y) && Math::NearlyEqual(v0.z, v1.z);
}

Vector3 Engine::Vector3::MakeContinuousDegrees(const Vector3& rawEuler, const Vector3& referenceEuler) {
	return { Math::MakeContinuousAngleDegrees(rawEuler.x, referenceEuler.x),
			Math::MakeContinuousAngleDegrees(rawEuler.y, referenceEuler.y),
			Math::MakeContinuousAngleDegrees(rawEuler.z, referenceEuler.z) };
}

//============================================================================
//	Vector3I structMethods
//============================================================================

Vector3I Vector3I::operator+(const Vector3I& other) const {
	return Vector3I(x + other.x, y + other.y, z + other.z);
}
Vector3I Vector3I::operator-(const Vector3I& other) const {
	return Vector3I(x - other.x, y - other.y, z - other.z);
}
Vector3I Vector3I::operator*(const Vector3I& other) const {
	return Vector3I(x * other.x, y * other.y, z * other.z);
}
Vector3I Vector3I::operator/(const Vector3I& other) const {
	return Vector3I(x / other.x, y / other.y, z / other.z);
}

Vector3I& Vector3I::operator+=(const Vector3I& v) {
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}
Vector3I& Vector3I::operator-=(const Vector3I& v) {
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}
Vector3I& Vector3I::operator*=(const Vector3I& v) {
	x *= v.x;
	y *= v.y;
	z *= v.z;
	return *this;
}
Vector3I& Vector3I::operator/=(const Vector3I& v) {
	x /= v.x;
	y /= v.y;
	z /= v.z;
	return *this;
}

Vector3I Vector3I::operator+(int32_t scalar) const {
	return Vector3I(x + scalar, y + scalar, z + scalar);
}
Vector3I Vector3I::operator-(int32_t scalar) const {
	return Vector3I(x - scalar, y - scalar, z - scalar);
}
Vector3I Vector3I::operator*(int32_t scalar) const {
	return Vector3I(x * scalar, y * scalar, z * scalar);
}
Vector3I Vector3I::operator/(int32_t scalar) const {
	return Vector3I(x / scalar, y / scalar, z / scalar);
}

Vector3I& Vector3I::operator+=(int32_t scalar) {
	x += scalar;
	y += scalar;
	z += scalar;
	return *this;
}
Vector3I& Vector3I::operator-=(int32_t scalar) {
	x -= scalar;
	y -= scalar;
	z -= scalar;
	return *this;
}
Vector3I& Vector3I::operator*=(int32_t scalar) {
	x *= scalar;
	y *= scalar;
	z *= scalar;
	return *this;
}
Vector3I& Vector3I::operator/=(int32_t scalar) {
	x /= scalar;
	y /= scalar;
	z /= scalar;
	return *this;
}

Vector3I Vector3I::operator-() const {
	return Vector3I(-x, -y, -z);
}

bool Vector3I::operator==(const Vector3I& other) const {
	return x == other.x && y == other.y && z == other.z;
}
bool Vector3I::operator!=(const Vector3I& other) const {
	return !(*this == other);
}
bool Vector3I::operator>=(const Vector3I& other) const {
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y) + static_cast<double>(z) * static_cast<double>(z)) >=
		std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) + static_cast<double>(other.y) * static_cast<double>(other.y) + static_cast<double>(other.z) * static_cast<double>(other.z));
}
bool Vector3I::operator<=(const Vector3I& other) const {
	return std::sqrt(static_cast<double>(x) * static_cast<double>(x) + static_cast<double>(y) * static_cast<double>(y) + static_cast<double>(z) * static_cast<double>(z)) <=
		std::sqrt(static_cast<double>(other.x) * static_cast<double>(other.x) + static_cast<double>(other.y) * static_cast<double>(other.y) + static_cast<double>(other.z) * static_cast<double>(other.z));
}

nlohmann::json Vector3I::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y}, {"z", z} };
}
Vector3I Vector3I::FromJson(const nlohmann::json& data) {
	Vector3I v{};
	if (data.is_array() && data.size() == 3) {
		v.x = data[0].get<int32_t>();
		v.y = data[1].get<int32_t>();
		v.z = data[2].get<int32_t>();
	} else if (data.contains("x") && data.contains("y") && data.contains("z")) {
		v.x = data["x"].get<int32_t>();
		v.y = data["y"].get<int32_t>();
		v.z = data["z"].get<int32_t>();
	}
	return v;
}

void Vector3I::Init() {
	this->x = 0;
	this->y = 0;
	this->z = 0;
}

void Vector3I::Init(int32_t value) {
	this->x = value;
	this->y = value;
	this->z = value;
}

std::vector<uint32_t> Vector3I::ToUInt() const {
	return { static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z) };
}