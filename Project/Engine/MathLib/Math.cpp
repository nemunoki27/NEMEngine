#include "Math.h"

//============================================================================
//	Math Methods
//============================================================================

namespace {

	// 値を[minValue, maxValue)の範囲に収める
	float WrapRange(float value, float minValue, float maxValue) {

		const float range = maxValue - minValue;
		if (range <= 0.0f) {
			return value;
		}

		while (value < minValue) {
			value += range;
		}
		while (value >= maxValue) {
			value -= range;
		}
		return value;
	}
}

float Math::RadToDeg(float rad) {

	return rad * (180.0f / pi);
}

Engine::Vector2 Math::RadToDeg(const Engine::Vector2& rad) {

	return { RadToDeg(rad.x), RadToDeg(rad.y) };
}

Engine::Vector3 Math::RadToDeg(const Engine::Vector3& rad) {

	return { RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z) };
}

Engine::Vector4 Math::RadToDeg(const Engine::Vector4& rad) {

	return { RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z), RadToDeg(rad.w) };
}

float Math::DegToRad(float deg) {

	return deg * (pi / 180.0f);
}

Engine::Vector2 Math::DegToRad(const Engine::Vector2& deg) {

	return { DegToRad(deg.x), DegToRad(deg.y) };
}

Engine::Vector3 Math::DegToRad(const Engine::Vector3& deg) {

	return { DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z) };
}

Engine::Vector4 Math::DegToRad(const Engine::Vector4& deg) {

	return { DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z), DegToRad(deg.w) };
}

float Math::WrapDegree360(float value) {

	return WrapRange(value, 0.0f, 360.0f);
}

float Math::WrapDegree180(float value) {

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

	return { WrapDegree360(value.x), WrapDegree360(value.y) };
}

Engine::Vector3 Math::WrapDegree360(const Engine::Vector3& value) {

	return { WrapDegree360(value.x), WrapDegree360(value.y), WrapDegree360(value.z) };
}

Engine::Vector2 Math::WrapDegree180(const Engine::Vector2& value) {

	return { WrapDegree180(value.x), WrapDegree180(value.y) };
}

Engine::Vector3 Math::WrapDegree180(const Engine::Vector3& value) {

	return { WrapDegree180(value.x), WrapDegree180(value.y), WrapDegree180(value.z) };
}

float Math::MakeContinuousAngleDegrees(float rawAngle, float referenceAngle) {

	return referenceAngle + std::remainder(rawAngle - referenceAngle, 360.0f);
}

void Math::MatrixToFloat16(const Engine::Matrix4x4& src, float out[16]) {

	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			out[row * 4 + col] = src.m[row][col];
		}
	}
}

Engine::Matrix4x4 Math::MatrixFromFloat16(const float in[16]) {

	Engine::Matrix4x4 result{};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			result.m[row][col] = in[row * 4 + col];
		}
	}
	return result;
}

bool Math::NearlyEqual(float lhs, float rhs) {

	return std::fabs(lhs - rhs) <= 0.001f;
}