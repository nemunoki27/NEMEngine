#include "Quaternion.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

//============================================================================
//	Quaternion structMethods
//============================================================================

Quaternion Quaternion::operator+(const Quaternion& other) const {
	return { x + other.x, y + other.y, z + other.z, w + other.w };
}
Quaternion Quaternion::operator-(const Quaternion& other) const {
	return { x - other.x, y - other.y, z - other.z, w - other.w };
}
Quaternion Quaternion::operator*(const Quaternion& other) const {
	Quaternion result;
	result.w = this->w * other.w - this->x * other.x - this->y * other.y - this->z * other.z;
	result.x = this->w * other.x + this->x * other.w + this->y * other.z - this->z * other.y;
	result.y = this->w * other.y - this->x * other.z + this->y * other.w + this->z * other.x;
	result.z = this->w * other.z + this->x * other.y - this->y * other.x + this->z * other.w;
	return result;
}
Quaternion Quaternion::operator/(const Quaternion& other) const {
	Quaternion result;
	result.w = this->w / other.w - this->x / other.x - this->y / other.y - this->z / other.z;
	result.x = this->w / other.x + this->x / other.w + this->y / other.z - this->z / other.y;
	result.y = this->w / other.y - this->x / other.z + this->y / other.w + this->z / other.x;
	result.z = this->w / other.z + this->x / other.y - this->y / other.x + this->z / other.w;
	return result;
}

Quaternion Quaternion::operator+(float scalar) const {
	return { x + scalar, y + scalar, z + scalar, w + scalar };
}
Quaternion Quaternion::operator-(float scalar) const {
	return { x - scalar, y - scalar, -scalar, w - scalar };
}
Quaternion Quaternion::operator*(float scalar) const {
	return { x * scalar, y * scalar, z * scalar, w * scalar };
}
Quaternion Quaternion::operator/(float scalar) const {
	return { x / scalar, y / scalar, z / scalar, w / scalar };
}

Quaternion Quaternion::operator-() const {
	return { -x, -y, -z, -w };
}

bool Quaternion::operator==(const Quaternion& other) const {
	return x == other.x && y == other.y && z == other.z && w == other.w;
}
bool Quaternion::operator!=(const Quaternion& other) const {
	return x != other.x || y != other.y || z != other.z || w != other.w;
}

nlohmann::json Quaternion::ToJson() const {
	return nlohmann::json{ {"x", x}, {"y", y}, {"z", z}, {"w", w} };
}

Quaternion Quaternion::FromJson(const nlohmann::json& data) {
	if (data.empty()) {
		return Quaternion::Identity();
	}
	Quaternion quaternion = Quaternion::Identity();
	if (data.is_array() && data.size() == 4) {
		quaternion.x = data[1].get<float>();
		quaternion.y = -data[3].get<float>();
		quaternion.z = -data[2].get<float>();
		quaternion.w = data[0].get<float>();
	} else if (data.contains("x") && data.contains("y") &&
		data.contains("z") && data.contains("w")) {
		quaternion.x = data.value("x", 0.0f);
		quaternion.y = data.value("y", 0.0f);
		quaternion.z = data.value("z", 0.0f);
		quaternion.w = data.value("w", 1.0f);
	}
	return quaternion;
}

void Quaternion::Init() {
	*this = Identity();
}

Quaternion Quaternion::Identity() {
	return { 0.0f, 0.0f, 0.0f, 1.0f };
}

float Quaternion::Length(const Quaternion& q) {
	return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

float Quaternion::Length() const {
	return std::sqrt(this->x * this->x + this->y * this->y + this->z * this->z + this->w * this->w);
}

Quaternion Quaternion::Normalize(const Quaternion& quaternion) {
	float norm = Length(quaternion);
	return { quaternion.x / norm, quaternion.y / norm, quaternion.z / norm, quaternion.w / norm };
}

Quaternion Quaternion::Normalize() const {
	float norm = Length(*this);
	return { this->x / norm, this->y / norm, this->z / norm, this->w / norm };
}

float Quaternion::Dot(const Quaternion& q0, const Quaternion& q1) {
	return q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
}

Quaternion Quaternion::Conjugate(const Quaternion& q) {
	return { -q.x, -q.y, -q.z, q.w };
}

Quaternion Quaternion::Inverse(const Quaternion& q) {
	Quaternion conjugate = Conjugate(q);
	float normSq = Length(q) * Length(q);
	return { conjugate.x / normSq, conjugate.y / normSq, conjugate.z / normSq, conjugate.w / normSq };
}

Quaternion Quaternion::MakeAxisAngle(const Vector3& axis, float angle) {
	Quaternion result{};
	float halfAngle = angle * 0.5f;
	float sinHalfAngle = std::sin(halfAngle);
	result.x = axis.x * sinHalfAngle;
	result.y = axis.y * sinHalfAngle;
	result.z = axis.z * sinHalfAngle;
	result.w = std::cos(halfAngle);
	return result;
}

Matrix4x4 Quaternion::MakeRotateMatrix(const Quaternion& q) {
	Matrix4x4 result;
	float xx = q.x * q.x;
	float yy = q.y * q.y;
	float zz = q.z * q.z;
	float ww = q.w * q.w;
	float xy = q.x * q.y;
	float xz = q.x * q.z;
	float yz = q.y * q.z;
	float wx = q.w * q.x;
	float wy = q.w * q.y;
	float wz = q.w * q.z;
	result.m[0][0] = ww + xx - yy - zz;
	result.m[0][1] = 2.0f * (xy + wz);
	result.m[0][2] = 2.0f * (xz - wy);
	result.m[0][3] = 0.0f;
	result.m[1][0] = 2.0f * (xy - wz);
	result.m[1][1] = ww - xx + yy - zz;
	result.m[1][2] = 2.0f * (yz + wx);
	result.m[1][3] = 0.0f;
	result.m[2][0] = 2.0f * (xz + wy);
	result.m[2][1] = 2.0f * (yz - wx);
	result.m[2][2] = ww - xx - yy + zz;
	result.m[2][3] = 0.0f;
	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;
	return result;
}

Quaternion Quaternion::Lerp(Quaternion q0, const Quaternion& q1, float lerpT) {
	// q0とq1の内積
	float dot = Dot(q0, q1);
	// 内積が負の場合、もう片方の回転を利用する
	if (dot < 0.0f) {
		q0 = -q0;
		dot = -dot;
	}
	if (dot >= 1.0f - FLT_EPSILON) {

		return q0 * (1.0f - lerpT) + q1 * lerpT;
	}
	// なす角を求める
	float theta = std::acos(dot);
	float sinTheta = std::sin(theta);
	// 補完係数を計算
	float scale0 = std::sin((1.0f - lerpT) * theta) / sinTheta;
	float scale1 = std::sin(lerpT * theta) / sinTheta;
	// 補完後のクォータニオンを求める
	return q0 * scale0 + q1 * scale1;
}

Quaternion Engine::Quaternion::FromToY(const Vector3& direction) {
	const Vector3 kY(0.0f, 1.0f, 0.0f);

	float dot = Vector3::Dot(kY, direction);
	// ほぼ同方向
	if (dot > 0.9999f) {
		return Quaternion::Identity();
	}
	// ほぼ逆方向
	if (dot < -0.9999f) {
		return Quaternion::MakeAxisAngle(Vector3(1.0f, 0.0f, 0.0f), Math::pi);
	}

	Vector3 axis = Vector3::Normalize(Vector3::Cross(kY, direction));
	float ang = std::acos(std::clamp(dot, -1.0f, 1.0f));
	return Quaternion::MakeAxisAngle(axis, ang);
}

Quaternion Quaternion::EulerToQuaternion(const Vector3& eulerDegrees) {

	return FromEulerDegrees(eulerDegrees);
}

Vector3 Quaternion::ToEulerAngles(const Quaternion& quaternion) {

	return ToEulerDegrees(quaternion);
}

Quaternion Engine::Quaternion::FromEulerRadians(const Vector3& eulerRadians) {

	const float hx = eulerRadians.x * 0.5f;
	const float hy = eulerRadians.y * 0.5f;
	const float hz = eulerRadians.z * 0.5f;

	const float sx = std::sin(hx);
	const float cx = std::cos(hx);
	const float sy = std::sin(hy);
	const float cy = std::cos(hy);
	const float sz = std::sin(hz);
	const float cz = std::cos(hz);

	Quaternion q{};
	q.x = sx * cy * cz - cx * sy * sz;
	q.y = cx * sy * cz + sx * cy * sz;
	q.z = cx * cy * sz - sx * sy * cz;
	q.w = cx * cy * cz + sx * sy * sz;

	return Normalize(q);
}

Quaternion Engine::Quaternion::FromEulerDegrees(const Vector3& eulerDegrees) {

	return FromEulerRadians(Math::DegToRad(eulerDegrees));
}

Vector3 Engine::Quaternion::ToEulerRadians(const Quaternion& quaternion) {

	const Quaternion q = Normalize(quaternion);
	const Matrix4x4 m = MakeRotateMatrix(q);

	Vector3 angles{};

	const float sy = std::clamp(-m.m[0][2], -1.0f, 1.0f);
	angles.y = std::asin(sy);

	const float cy = std::cos(angles.y);

	if (std::fabs(cy) > 1e-6f) {
		angles.x = std::atan2(m.m[1][2], m.m[2][2]);
		angles.z = std::atan2(m.m[0][1], m.m[0][0]);
	} else {
		// Gimbal lock
		angles.x = std::atan2(-m.m[2][1], m.m[1][1]);
		angles.z = 0.0f;
	}
	return angles;
}

Vector3 Engine::Quaternion::ToEulerDegrees(const Quaternion& q) {

	return Math::RadToDeg(ToEulerRadians(q));
}

bool Engine::Quaternion::NearlyEqual(const Quaternion& q0, const Quaternion& q1) {

	float dot = std::fabs(Quaternion::Dot(q0, q1));
	return  (1.0f - 0.001f) <= dot;
}