#include "Quaternion.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <limits>

using namespace Engine;

//============================================================================
//	Quaternion structMethods
//============================================================================
Quaternion Quaternion::operator+(const Quaternion& other) const {

	// 対応する成分を加算
	return {x + other.x, y + other.y, z + other.z, w + other.w};
}

Quaternion Quaternion::operator-(const Quaternion& other) const {

	// 対応する成分を減算
	return {x - other.x, y - other.y, z - other.z, w - other.w};
}

Quaternion Quaternion::operator*(const Quaternion& other) const {

	// 回転の積を計算
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

	// 対応する成分を加算
	return {x + scalar, y + scalar, z + scalar, w + scalar};
}

Quaternion Quaternion::operator-(float scalar) const {

	// 対応する成分を減算
	return {x - scalar, y - scalar, z - scalar, w - scalar};
}

Quaternion Quaternion::operator*(float scalar) const {

	// 各成分の積を計算
	return {x * scalar, y * scalar, z * scalar, w * scalar};
}

Quaternion Quaternion::operator/(float scalar) const {

	// 各成分を除算
	return {x / scalar, y / scalar, z / scalar, w / scalar};
}

Quaternion Quaternion::operator-() const {

	// 全成分の符号を反転
	return {-x, -y, -z, -w};
}

bool Quaternion::operator==(const Quaternion& other) const {

	// 全成分を比較
	return x == other.x && y == other.y && z == other.z && w == other.w;
}

bool Quaternion::operator!=(const Quaternion& other) const {

	// 全成分を比較
	return x != other.x || y != other.y || z != other.z || w != other.w;
}

void Quaternion::Init() {

	// 各成分を初期化
	*this = Identity();
}

Quaternion Quaternion::Identity() {

	// 単位値を作成
	return {0.0f, 0.0f, 0.0f, 1.0f};
}

float Quaternion::Length(const Quaternion& q) {

	// 各成分から長さを計算
	return static_cast<float>(std::sqrt(Math::SquaredLength(q.x, q.y, q.z, q.w)));
}

float Quaternion::Length() const {

	// 各成分から長さを計算
	return Length(*this);
}

Quaternion Quaternion::Normalize(const Quaternion& quaternion) {

	// 二乗の桁あふれを避けて回転の長さを求める
	double norm = std::hypot(std::hypot(static_cast<double>(quaternion.x), quaternion.y),
		std::hypot(static_cast<double>(quaternion.z), quaternion.w));
	if (!(norm > 0.0) || !std::isfinite(norm)) {
		return Identity();
	}
	return {static_cast<float>(quaternion.x / norm), static_cast<float>(quaternion.y / norm),
		static_cast<float>(quaternion.z / norm), static_cast<float>(quaternion.w / norm)};
}

Quaternion Quaternion::Normalize() const {

	// 長さを揃えて正規化
	return Normalize(*this);
}

float Quaternion::Dot(const Quaternion& q0, const Quaternion& q1) {

	// 対応する成分の積を加算
	return q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
}

Quaternion Quaternion::Conjugate(const Quaternion& q) {

	// 虚数成分の符号を反転
	return {-q.x, -q.y, -q.z, q.w};
}

Quaternion Quaternion::Inverse(const Quaternion& q) {

	// 逆変換を計算
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
	const double values[] = {-q.x / normSquared, -q.y / normSquared, -q.z / normSquared, q.w / normSquared};
	for (double value : values) {
		if (std::abs(value) > (std::numeric_limits<float>::max)()) {
			return false;
		}
	}
	// 全成分を変換できる場合だけ出力する
	output = {static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]),
		static_cast<float>(values[3])};
	return true;
}

Quaternion Quaternion::MakeAxisAngle(const Vector3& axis, float angle) {

	// 軸と半角から回転を作成
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

	// 回転行列を作成
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

	// Y軸を指定方向へ合わせる
	const Vector3 kY(0.0f, 1.0f, 0.0f);
	const Vector3 normalized = Vector3::NormalizeOr(direction, {}, 0.0f);
	if (normalized == Vector3{}) {
		return Identity();
	}

	float dot = Vector3::Dot(kY, normalized);
	// ほぼ同方向
	if (dot > 0.9999f) {
		return Quaternion::Identity();
	}
	// ほぼ逆方向
	if (dot < -0.9999f) {
		return Quaternion::MakeAxisAngle(Vector3(1.0f, 0.0f, 0.0f), Math::pi);
	}

	Vector3 axis = Vector3::Normalize(Vector3::Cross(kY, normalized));
	float ang = std::acos(std::clamp(dot, -1.0f, 1.0f));
	return Quaternion::MakeAxisAngle(axis, ang);
}

Quaternion Engine::Quaternion::LookRotation(const Vector3& forward, const Vector3& up) {

	// 前方向をローカルZ軸へ合わせる
	const Vector3 axisZ = Vector3::NormalizeOr(forward, {}, 1e-6f);
	if (axisZ == Vector3{}) {
		return Quaternion::Identity();
	}

	// 上方向が平行なら別の基準軸を使う
	const Vector3 normalizedUp = Vector3::NormalizeOr(up, {}, 0.0f);
	Vector3 axisX = Vector3::NormalizeOr(Vector3::Cross(normalizedUp, axisZ), {}, 1e-6f);
	if (axisX == Vector3{}) {

		const Vector3 fallbackUp = std::fabs(axisZ.y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
		axisX = Vector3::NormalizeOr(Vector3::Cross(fallbackUp, axisZ), {}, 0.0f);
	}
	const Vector3 axisY = Vector3::Cross(axisZ, axisX);

	// 行ベクトル行列から共通処理で回転を復元
	Matrix4x4 matrix = Matrix4x4::Identity();
	matrix.m[0][0] = axisX.x;
	matrix.m[0][1] = axisX.y;
	matrix.m[0][2] = axisX.z;
	matrix.m[1][0] = axisY.x;
	matrix.m[1][1] = axisY.y;
	matrix.m[1][2] = axisY.z;
	matrix.m[2][0] = axisZ.x;
	matrix.m[2][1] = axisZ.y;
	matrix.m[2][2] = axisZ.z;
	return QuaternionFromRotationMatrixRowVector(matrix);
}

Quaternion Quaternion::EulerToQuaternion(const Vector3& eulerDegrees) {

	// 度数法の回転を変換
	return FromEulerDegrees(eulerDegrees);
}

Vector3 Quaternion::ToEulerAngles(const Quaternion& quaternion) {

	// 回転を度数法へ変換
	return ToEulerDegrees(quaternion);
}

Quaternion Engine::Quaternion::FromEulerRadians(const Vector3& eulerRadians) {

	// 各軸の半角から回転を合成
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

	// 度数法をラジアンへ変換
	return FromEulerRadians(Math::DegToRad(eulerDegrees));
}

Vector3 Engine::Quaternion::ToEulerRadians(const Quaternion& quaternion) {

	// 回転行列から各軸の角度を取得
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
		// 特異姿勢ではZ回転を固定
		angles.x = std::atan2(-m.m[2][1], m.m[1][1]);
		angles.z = 0.0f;
	}
	return angles;
}

Vector3 Engine::Quaternion::ToEulerDegrees(const Quaternion& q) {

	// 各軸の角度を度数法へ変換
	return Math::RadToDeg(ToEulerRadians(q));
}

bool Engine::Quaternion::NearlyEqual(const Quaternion& q0, const Quaternion& q1) {

	// 許容誤差の範囲で比較
	float dot = std::fabs(Quaternion::Dot(q0, q1));
	return (1.0f - 0.001f) <= dot;
}
