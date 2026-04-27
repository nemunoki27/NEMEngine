#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Vector4.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Color.h>

// windows
#include <windows.h>
#include <windef.h>
// c++
#include <cmath>
#include <numbers>
#include <vector>
#include <algorithm>

//============================================================================
//	Math namespace
//============================================================================
namespace Math {

	constexpr float pi = std::numbers::pi_v<float>;
	constexpr float radian = pi / 180.0f;

	//============================================================================
	//	角度変換
	//============================================================================

	// ラジアン → 度
	float RadToDeg(float rad);
	Engine::Vector2 RadToDeg(const Engine::Vector2& rad);
	Engine::Vector3 RadToDeg(const Engine::Vector3& rad);
	Engine::Vector4 RadToDeg(const Engine::Vector4& rad);

	// 度 → ラジアン
	float DegToRad(float deg);
	Engine::Vector2 DegToRad(const Engine::Vector2& deg);
	Engine::Vector3 DegToRad(const Engine::Vector3& deg);
	Engine::Vector4 DegToRad(const Engine::Vector4& deg);

	//============================================================================
	//	角度正規化
	//============================================================================

	// [0, 360)
	float WrapDegree360(float value);
	// [-180, 180)
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

	// 行列をfloat16の配列に変換する
	void MatrixToFloat16(const Engine::Matrix4x4& src, float out[16]);
	Engine::Matrix4x4 MatrixFromFloat16(const float in[16]);

	//============================================================================
	//	汎用
	//============================================================================

	// 近似比較
	bool NearlyEqual(float lhs, float rhs);
} // Math