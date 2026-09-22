#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

namespace Engine::AnimationGroupLookup {

	const AnimationState* FindStateInGroup(const AnimationGroup& group, const std::string& name);

	bool SameBinding(const AnimationPropertyBinding& lhs, const AnimationPropertyBinding& rhs);

	const AnimationGroup* FindGroup( const AnimationPlayerComponent& player, const std::string& name);
}
