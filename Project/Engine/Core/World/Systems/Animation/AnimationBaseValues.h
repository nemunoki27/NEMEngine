#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationBaseValues {

	void CaptureBaseValues(ECSWorld& world, const Entity& entity,
		const AnimationPlayerComponent& player, SystemContext& context, std::vector<AnimationPreviewBaseValue>& out);

	void RestoreBaseValues(ECSWorld& world, const Entity& entity, const std::vector<AnimationPreviewBaseValue>& base);
}
