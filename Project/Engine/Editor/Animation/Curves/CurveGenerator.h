#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	CurveGenerator
	//	波形やイージングからカーブへキーをまとめて生成する、各ツールのカーブ編集で共用する
	//============================================================================
	// 生成する波形の種類
	enum class CurveGeneratorType :
		uint8_t {

		Sin,
		Cos,
		Easing,
	};

	// 生成条件、ツールごとに保持して使い回す
	struct CurveGeneratorState {

		// 生成する波形の種類
		CurveGeneratorType type = CurveGeneratorType::Sin;
		// 生成範囲の時間と値
		float startTime = 0.0f;
		float endTime = 1.0f;
		float startValue = 0.0f;
		float endValue = 1.0f;
		// 波形のパラメータ
		float amplitude = 1.0f;
		float frequency = 1.0f;
		float phase = 0.0f;
		// イージング
		EasingType easingType = EasingType::Linear;
		// 生成するキー数
		int32_t sampleCount = 16;
		// 範囲内の既存キーを置き換えるか
		bool replaceKeys = true;
		// 適用先の選択位置
		int32_t targetIndex = 0;
		// キー時刻の上限、0以下で無制限
		float maxKeyTime = 0.0f;
	};

	// ベイクの適用先候補、ラベルと対象チャネルindexの組で表す
	struct CurveBakeTarget {

		std::string label;
		std::vector<uint32_t> channelIndices;
	};

	// 生成条件と適用先のUIを描画し、生成ボタンで対象チャンネルへキーを生成する、生成したらtrue
	bool DrawCurveGenerator(CurveGeneratorState& state,
		std::span<CurveChannel> channels, std::span<const CurveBakeTarget> targets);
} // Engine
