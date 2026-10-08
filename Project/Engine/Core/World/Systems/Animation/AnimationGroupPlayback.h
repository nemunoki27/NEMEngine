#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationGroupPlayback {

	void BeginGroup(AnimationPlayerComponent& player, const std::string& groupName, float fadeDuration);

	void AdvanceClip(AnimationPlayerComponent& player,
		const AnimationGroup& group, AnimationClipRuntime& clipRt, SystemContext& context,
		std::vector<AnimationEvent>* firedOut);
}
