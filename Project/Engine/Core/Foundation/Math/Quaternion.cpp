#include "Quaternion.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>
#include <limits>

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

	// 逆回転との積で右側の回転を取り除く
	return *this * Inverse(other);
}

Quaternion Quaternion::operator+(float scalar) const {
	return { x + scalar, y + scalar, z + scalar, w + scalar };
}
Quaternion Quaternion::operator-(float scalar) const {
	return { x - scalar, y - scalar, z - scalar, w - scalar };
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
		// 配列もobjectと同じxyzw順で読み込む
		quaternion.x = data[0].get<float>();
		quaternion.y = data[1].get<float>();
		quaternion.z = data[2].get<float>();
		quaternion.w = data[3].get<float>();
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

	// 二乗の桁あふれを避けて回転の長さを求める
	double norm = std::hypot(std::hypot(static_cast<double>(quaternion.x), quaternion.y),
		std::hypot(static_cast<double>(quaternion.z), quaternion.w));
	if (!(norm > 0.0) || !std::isfinite(norm)) {
		return Identity();
	}
	return { static_cast<float>(quaternion.x / norm), static_cast<float>(quaternion.y / norm),
		static_cast<float>(quaternion.z / norm), static_cast<float>(quaternion.w / norm) };
}

Quaternion Quaternion::Normalize() const {

	return Normalize(*this);
}

float Quaternion::Dot(const Quaternion& q0, const Quaternion& q1) {
	return q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
}

Quaternion Quaternion::Conjugate(const Quaternion& q) {
	return { -q.x, -q.y, -q.z, q.w };
}

Quaternion Quaternion::Inverse(const Quaternion& q) {

	Quaternion result = Identity();
	TryInverse(q, result);
	return result;
}

bool Quaternion::TryInverse(const Quaternion& q, Quaternion& output) {

	// 二乗和をdoubleで計算し、巨大値のoverflowを避ける
	const double normSquared = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
		static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
	if (!std::isfinite(normSquared) || normSquared == 0.0) {
		return false;
	}
	const double values[] = { -q.x / normSquared, -q.y / normSquared, -q.z / normSquared, q.w / normSquared };
	for (double value : values) {
		if (std::abs(value) > (std::numeric_limits<float>::max)()) {
			return false;
		}
	}
	// 全成分を変換できる場合だけ出力する
	output = { static_cast<float>(values[0]), static_cast<float>(values[1]),
		static_cast<float>(values[2]), static_cast<float>(values[3]) };
	return true;
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

	// 入力を単位回転へ揃えて最短側を補間する
	q0 = Normalize(q0);
	const Quaternion end = Normalize(q1);
	// q0とq1の内積
	float dot = std::clamp(Dot(q0, end), -1.0f, 1.0f);
	// 内積が負の場合、もう片方の回転を利用する
	if (dot < 0.0f) {
		q0 = -q0;
		dot = -dot;
	}
	if (dot >= 1.0f - FLT_EPSILON) {

		return Normalize(q0 * (1.0f - lerpT) + end * lerpT);
	}
	// なす角を求める
	float theta = std::acos(dot);
	float sinTheta = std::sin(theta);
	// 補完係数を計算
	float scale0 = std::sin((1.0f - lerpT) * theta) / sinTheta;
	float scale1 = std::sin(lerpT * theta) / sinTheta;
	// 補完後のクォータニオンを求める
	return Normalize(q0 * scale0 + end * scale1);
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

Quaternion Engine::Quaternion::LookRotation(const Vector3& forward, const Vector3& up) {

	// forwardをローカル+Zに合わせる、長さが無ければ回転なし
	const float forwardLength = forward.Length();
	if (forwardLength <= 1e-6f) {
		return Quaternion::Identity();
	}
	const Vector3 axisZ = forward * (1.0f / forwardLength);

	// 右ベクトルはup×forward、upがforwardと平行なら別の基準upでやり直す
	Vector3 right = Vector3::Cross(up, axisZ);
	float rightLength = right.Length();
	if (rightLength <= 1e-6f) {

		const Vector3 fallbackUp = std::fabs(axisZ.y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
		right = Vector3::Cross(fallbackUp, axisZ);
		rightLength = right.Length();
	}
	const Vector3 axisX = right * (1.0f / rightLength);
	const Vector3 axisY = Vector3::Cross(axisZ, axisX);

	// 各軸を行に並べた回転行列はMakeRotateMatrixと同じレイアウト、そこからクォータニオンを復元する
	const float m00 = axisX.x, m01 = axisX.y, m02 = axisX.z;
	const float m10 = axisY.x, m11 = axisY.y, m12 = axisY.z;
	const float m20 = axisZ.x, m21 = axisZ.y, m22 = axisZ.z;

	Quaternion result{};
	const float trace = m00 + m11 + m22;
	if (trace > 0.0f) {

		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		result.w = 0.25f * s;
		result.x = (m12 - m21) / s;
		result.y = (m20 - m02) / s;
		result.z = (m01 - m10) / s;
	} else if (m00 > m11 && m00 > m22) {

		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		result.w = (m12 - m21) / s;
		result.x = 0.25f * s;
		result.y = (m01 + m10) / s;
		result.z = (m20 + m02) / s;
	} else if (m11 > m22) {

		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		result.w = (m20 - m02) / s;
		result.x = (m01 + m10) / s;
		result.y = 0.25f * s;
		result.z = (m12 + m21) / s;
	} else {

		const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
		result.w = (m01 - m10) / s;
		result.x = (m20 + m02) / s;
		result.y = (m12 + m21) / s;
		result.z = 0.25f * s;
	}
	return Normalize(result);
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
