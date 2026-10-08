#pragma once

#include "ParticleLoopSettings.h"
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	// 寿命進行度から求める数値の設定
	struct ParticleFloatAnimationSettings {

		float start = 0.0f;
		float end = 1.0f;
		EasingType easingType = EasingType::EaseOutSine;
		ParticleLoopSettings loop{};
		bool useCurve = false;
		CurveFloat curve{};
	};

	namespace ParticleFloatAnimation {

		// 省略項目は既存値を維持して読み込む
		void ReadAnimationSettings(const nlohmann::json& in, ParticleFloatAnimationSettings& settings);
		// 保存形式へ変換する
		nlohmann::json WriteAnimationSettings(const ParticleFloatAnimationSettings& settings);
		// ループ後の進行度から値を求める
		float EvaluateAnimation(const ParticleFloatAnimationSettings& settings, float rawT);
	}
}
