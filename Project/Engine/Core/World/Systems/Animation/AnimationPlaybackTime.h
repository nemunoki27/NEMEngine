#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationPlaybackTime {

	AnimationWrapMode EffectiveWrap(AnimationWrapMode wrap, bool clipLoop);

	float AdvanceTime(float duration, AnimationWrapMode wrap, float time, int8_t& dir, float delta, bool& finished);

	bool CompleteLoopIteration(AnimationClipRuntime& rt, const AnimationState& state, float dur);

	void AdvanceLoop(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta);

	void AdvancePingPong(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta);
}
