#include "Math.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <limits>

//============================================================================
//	Math Methods
//============================================================================
namespace {

	// 小数部を切り捨てて変換先の範囲を確認する
	template<typename T>
	bool TryConvertInteger(float value, T& result) {

		if (!std::isfinite(value)) {
			return false;
		}
		const double truncated = std::trunc(static_cast<double>(value));
		if (truncated < static_cast<double>(std::numeric_limits<T>::lowest()) ||
			truncated > static_cast<double>(std::numeric_limits<T>::max())) {
			return false;
		}
		result = static_cast<T>(truncated);
		return true;
	}

	// 値を[minValue, maxValue)の範囲に収める
	float WrapRange(float value, float minValue, float maxValue) {

		const float range = maxValue - minValue;
		if (range <= 0.0f) {
			return value;
		}

		// 非有限値でループせず、無効な角度をそのまま伝える
		if (!std::isfinite(value)) {
			return value;
		}
		// 大きな角度も剰余で一度に折り返す
		float wrapped = std::fmod(value - minValue, range);
		if (wrapped < 0.0f) {
			wrapped += range;
		}
		return wrapped < range ? minValue + wrapped : minValue;
	}
}

float Math::RadToDeg(float rad) {

	// ラジアンを度数法へ変換
	return rad * (180.0f / pi);
}

Engine::Vector2 Math::RadToDeg(const Engine::Vector2& rad) {

	// ラジアンを度数法へ変換
	return {RadToDeg(rad.x), RadToDeg(rad.y)};
}

Engine::Vector3 Math::RadToDeg(const Engine::Vector3& rad) {

	// ラジアンを度数法へ変換
	return {RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z)};
}

Engine::Vector4 Math::RadToDeg(const Engine::Vector4& rad) {

	// ラジアンを度数法へ変換
	return {RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z), RadToDeg(rad.w)};
}

float Math::DegToRad(float deg) {

	// 度数法をラジアンへ変換
	return deg * (pi / 180.0f);
}

Engine::Vector2 Math::DegToRad(const Engine::Vector2& deg) {

	// 度数法をラジアンへ変換
	return {DegToRad(deg.x), DegToRad(deg.y)};
}

Engine::Vector3 Math::DegToRad(const Engine::Vector3& deg) {

	// 度数法をラジアンへ変換
	return {DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z)};
}

Engine::Vector4 Math::DegToRad(const Engine::Vector4& deg) {

	// 度数法をラジアンへ変換
	return {DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z), DegToRad(deg.w)};
}

float Math::WrapDegree360(float value) {

	// 角度を0から360度の範囲へ戻す
	return WrapRange(value, 0.0f, 360.0f);
}

float Math::WrapDegree180(float value) {

	// 角度を符号付きの半周期へ戻す
	value = std::remainder(value, 360.0f);
	if (value <= -180.0f) {
		value += 360.0f;
	}
	if (value > 180.0f) {
		value -= 360.0f;
	}
	return value;
}

Engine::Vector2 Math::WrapDegree360(const Engine::Vector2& value) {

	// 角度を0から360度の範囲へ戻す
	return {WrapDegree360(value.x), WrapDegree360(value.y)};
}

Engine::Vector3 Math::WrapDegree360(const Engine::Vector3& value) {

	// 角度を0から360度の範囲へ戻す
	return {WrapDegree360(value.x), WrapDegree360(value.y), WrapDegree360(value.z)};
}

Engine::Vector2 Math::WrapDegree180(const Engine::Vector2& value) {

	// 角度を符号付きの半周期へ戻す
	return {WrapDegree180(value.x), WrapDegree180(value.y)};
}

Engine::Vector3 Math::WrapDegree180(const Engine::Vector3& value) {

	// 角度を符号付きの半周期へ戻す
	return {WrapDegree180(value.x), WrapDegree180(value.y), WrapDegree180(value.z)};
}

float Math::MakeContinuousAngleDegrees(float rawAngle, float referenceAngle) {

	// 差分の桁あふれを避けて近い周期へ揃える
	const double difference = static_cast<double>(rawAngle) - referenceAngle;
	return static_cast<float>(referenceAngle + std::remainder(difference, 360.0));
}

void Math::MatrixToFloat16(const Engine::Matrix4x4& src, float out[16]) {

	// 行の順に16成分を格納
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			out[row * 4 + col] = src.m[row][col];
		}
	}
}

Engine::Matrix4x4 Math::MatrixFromFloat16(const float in[16]) {

	// 16成分を行列へ配置
	Engine::Matrix4x4 result{};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			result.m[row][col] = in[row * 4 + col];
		}
	}
	return result;
}

bool Math::NearlyEqual(float lhs, float rhs) {

	// 差分を許容誤差と比較
	return std::fabs(lhs - rhs) <= 0.001f;
}

bool Math::TryConvertToInt32(float value, int32_t& result) {

	return TryConvertInteger(value, result);
}

bool Math::TryConvertToUInt32(float value, uint32_t& result) {

	return TryConvertInteger(value, result);
}

float Math::Lerp(float a, float b, float t) {

	// 両端の値を線形補間
	return a + (b - a) * t;
}

float Math::Saturate(float v) {

	// 値を0から1の範囲へ収める
	return std::clamp(v, 0.0f, 1.0f);
}
