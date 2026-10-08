#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <numbers>
#include <cstdint>

//============================================================================
//	Math namespace
//============================================================================
namespace Math {

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float radian = pi / 180.0f;

	//============================================================================
	//	角度変換
	//============================================================================

	// ラジアン→度
	float RadToDeg(float rad);
	Engine::Vector2 RadToDeg(const Engine::Vector2& rad);
	Engine::Vector3 RadToDeg(const Engine::Vector3& rad);
	Engine::Vector4 RadToDeg(const Engine::Vector4& rad);

	// 度→ラジアン
	float DegToRad(float deg);
	Engine::Vector2 DegToRad(const Engine::Vector2& deg);
	Engine::Vector3 DegToRad(const Engine::Vector3& deg);
	Engine::Vector4 DegToRad(const Engine::Vector4& deg);

	//============================================================================
	//	角度正規化
	//============================================================================

	// [0, 360)
	float WrapDegree360(float value);
	// -180度を除き180度を含む範囲へ戻す
	float WrapDegree180(float value);

	Engine::Vector2 WrapDegree360(const Engine::Vector2& value);
	Engine::Vector3 WrapDegree360(const Engine::Vector3& value);
	Engine::Vector2 WrapDegree180(const Engine::Vector2& value);
	Engine::Vector3 WrapDegree180(const Engine::Vector3& value);

	// アングルを参照角度に最も近い360度系へ寄せる
	float MakeContinuousAngleDegrees(float rawAngle, float referenceAngle);

	//============================================================================
	//	数学
	//============================================================================

	// float成分の二乗を桁あふれせず合計する
	constexpr double SquaredLength(float x, float y, float z = 0.0f, float w = 0.0f) {

		return static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z +
			   static_cast<double>(w) * w;
	}

	// 行列をfloat16の配列に変換する
	void MatrixToFloat16(const Engine::Matrix4x4& src, float out[16]);
	Engine::Matrix4x4 MatrixFromFloat16(const float in[16]);

	//============================================================================
	//	汎用
	//============================================================================

	// 近似比較
	bool NearlyEqual(float lhs, float rhs);
	// 有限な値を符号付き32bit整数へ変換する
	bool TryConvertToInt32(float value, int32_t& result);
	// 有限な値を符号なし32bit整数へ変換する
	bool TryConvertToUInt32(float value, uint32_t& result);

	// 線形補間
	float Lerp(float a, float b, float t);
	// [0, 1]へクランプ
	float Saturate(float v);
}
