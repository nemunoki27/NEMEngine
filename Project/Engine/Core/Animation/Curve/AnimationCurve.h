#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

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
	enum class CurveInterpolationMode :
		uint8_t {

		Constant, // 次のキーまで現在値を維持する
		Linear,   // 前後のキーを直線でつなぐ
		Cubic,    // 自動接線を使って滑らかにつなぐ
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
		CurveInterpolationMode interpolation = CurveInterpolationMode::Linear;
	};
	// 1つの値成分を持つカーブ
	struct CurveChannel {

		// Inspectorやチャンネル一覧に表示する名前
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
		float Evaluate(float time) const;
	};
	struct CurveVector3 {

		std::array<CurveChannel, 3> channels;

		CurveVector3();
		Vector3 Evaluate(float time) const;
	};
	struct CurveColor3 {

		std::array<CurveChannel, 3> channels;

		CurveColor3();
		Color3 Evaluate(float time) const;
	};
	struct CurveColor4 {

		std::array<CurveChannel, 4> channels;

		CurveColor4();
		Color4 Evaluate(float time) const;
	};

	// 共通処理
	// カーブ型ごとに編集対象チャンネルをspanで取得する
	std::span<CurveChannel> GetCurveChannels(CurveFloat& curve);
	std::span<CurveChannel> GetCurveChannels(CurveVector3& curve);
	std::span<CurveChannel> GetCurveChannels(CurveColor3& curve);
	std::span<CurveChannel> GetCurveChannels(CurveColor4& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveFloat& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveVector3& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveColor3& curve);
	std::span<const CurveChannel> GetCurveChannels(const CurveColor4& curve);
} // Engine
