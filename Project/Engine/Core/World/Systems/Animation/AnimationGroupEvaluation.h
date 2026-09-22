#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationGroupEvaluation {

	void EvaluateGroupClips(ECSWorld& world, const Entity& entity,
		const AnimationGroup& group, const std::vector<AnimationClipRuntime>& clips,
		std::span<const AnimationPreviewBaseValue> baseStore, SystemContext& context,
		std::vector<AnimationEvaluatedValue>& outValues);
}
