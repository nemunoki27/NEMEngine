#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>

// c++
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	AnimationCurve enum class
	//============================================================================
	// キー間を補間する方法
	enum class CurveInterpolationMode : uint8_t {

		Constant, // 次のキーまで現在値を維持する
		Linear,   // 前後のキーを直線でつなぐ
		Bezier,   // 手動接線を使ってつなぐ
		Spline,   // 自動接線を使って滑らかにつなぐ
		Squad,    // 回転専用で数値カーブではSplineとして扱う
	};

	//============================================================================
	//	AnimationCurve structures
	//============================================================================
	// カーブ上の1キー
	struct CurveKey {

		// キーを置く時間
		float time = 0.0f;
		// キーの値
		float value = 0.0f;
		// 次のキーまでの補間方法
		CurveInterpolationMode interpolation = CurveInterpolationMode::Spline;
		// Bezierの接線を時間と値の相対座標で保持する
		Vector2 inTangent = Vector2::AnyInit(0.0f);
		Vector2 outTangent = Vector2::AnyInit(0.0f);
	};
	// 1つの値成分を持つカーブ
	struct CurveChannel {

		// チャンネルの表示名
		std::string name = "Value";
		// カーブ線とキーの表示色
		Color4 displayColor = Color4::White();
		// キーがない場合に返す値
		float defaultValue = 0.0f;
		// 時間順に並べるキー配列
		std::vector<CurveKey> keys;

		// 指定時間の値を補間して取得する
		float Evaluate(float time) const;
		// キーを追加して時間順に並べ、追加後のキー番号を返す
		uint32_t AddKey(float time, float value, CurveInterpolationMode interpolation = CurveInterpolationMode::Linear);
		// 指定番号のキーを削除する
		bool RemoveKey(uint32_t index);
		// キーを時間順に並べ替える
		void SortKeys();
		// キー全体の時間範囲を取得する
		bool GetTimeRange(float& outMinTime, float& outMaxTime) const;
		// キー全体の値範囲を取得する
		bool GetValueRange(float& outMinValue, float& outMaxValue) const;
	};

	// カーブの種類ごとの構造体
	struct CurveFloat {

		CurveChannel channel;

		CurveFloat();
		// 指定時間の値を取得する
		float Evaluate(float time) const;
	};
	struct CurveVector3 {

		std::array<CurveChannel, 3> channels;

		CurveVector3();
		// 各成分の値を合成する
		Vector3 Evaluate(float time) const;
	};
	struct CurveColor3 {

		std::array<CurveChannel, 3> channels;

		CurveColor3();
		// RGBの値を合成する
		Color3 Evaluate(float time) const;
	};
	struct CurveColor4 {

		std::array<CurveChannel, 4> channels;

		CurveColor4();
		// RGBAの値を合成する
		Color4 Evaluate(float time) const;
	};
	struct CurveQuaternionAxisKey {

		// 任意の回転軸を使用する
		bool useCustomAxis = false;
		std::vector<Axis> axes{ Axis::X };
		Vector3 customAxis = Vector3(1.0f, 0.0f, 0.0f);
	};
	struct CurveQuaternion {

		// 軸と角度のチャンネル
		std::array<CurveChannel, 2> channels;
		// 軸チャンネルのキーに対応する回転軸
		std::vector<CurveQuaternionAxisKey> axisKeys;

		CurveQuaternion();
		// 指定時間の回転軸を取得する
		Vector3 EvaluateAxis(float time) const;
		// 指定時間の回転角度を取得する
		float EvaluateAngle(float time) const;
		// 回転軸と角度からQuaternionを作る
		Quaternion Evaluate(float time) const;
		// 軸設定の数をキー数へ揃える
		void EnsureAxisKeyCount();
	};

	// 共通処理
	// カーブの種類に対応するチャンネルを取得する
	std::span<CurveChannel> GetCurveChannels(CurveFloat& curve);
	std::span<CurveChannel> GetCurveChannels(CurveVector3& curve);
	std::span<CurveChannel> GetCurveChannels(CurveColor3& curve);
	std::span<CurveChannel> GetCurveChannels(CurveColor4& curve);
	std::span<CurveChannel> GetCurveChannels(CurveQuaternion& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveFloat& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveVector3& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveColor3& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveColor4& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveQuaternion& curve);
} // Engine
