#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationPlaybackTime {

	struct Interval {

		float from = 0.0f;
		float to = 0.0f;
		bool includeStart = false;
	};

	AnimationWrapMode EffectiveWrap(AnimationWrapMode wrap, bool clipLoop);

	float AdvanceTime(float duration, AnimationWrapMode wrap, float time, int8_t& dir, float delta, bool& finished);

	bool CompleteLoopIteration(AnimationClipRuntime& rt, const AnimationState& state, float dur);

	void AdvanceLoop(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta,
		std::vector<Interval>* intervals = nullptr);

	void AdvancePingPong(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta,
		std::vector<Interval>* intervals = nullptr);
	void RecordInterval(AnimationClipRuntime& rt, float from, float to, std::vector<Interval>* intervals);
}
