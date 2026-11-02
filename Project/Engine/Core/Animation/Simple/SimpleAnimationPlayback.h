#pragma once

//============================================================================
//	include
//============================================================================
#include "SimpleAnimationEvaluation.h"

namespace Engine {

	// 設定から独立した単一値の再生状態
	struct SimpleAnimationPlayback {

		double time = 0.0;
		SimpleAnimationValue initial = 0.0f;
		SimpleAnimationSample sample{};
		bool playing = false;
		bool paused = false;
		bool initialized = false;
		size_t type = 0;
		SimpleAnimationMode mode = SimpleAnimationMode::Interpolate;
	};

	void StartSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings,
		const SimpleAnimationValue& initial);
	void SampleSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings);
	void AdvanceSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings, double deltaTime);
}
