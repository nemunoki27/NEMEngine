#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationEventCollection {

	void CollectClipEvents(const AnimationClipAsset& clip, AnimationWrapMode wrap,
		float beforeTime, int8_t beforeDir, AnimationClipPhase beforePhase, int32_t beforeRepeat,
		const AnimationClipRuntime& clipRt, float dur, std::vector<AnimationEvent>& firedOut);
}
