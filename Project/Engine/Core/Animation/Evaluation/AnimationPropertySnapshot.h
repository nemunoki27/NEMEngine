#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipEvaluator.h"

namespace Engine::AnimationPropertySnapshot {

	bool Contains(std::span<const AnimationPreviewBaseValue> values, const AnimationPropertyBinding& binding);
	bool Capture(ECSWorld& world, const Entity& entity, const AnimationPropertyBinding& binding,
		std::vector<AnimationPreviewBaseValue>& values);
	void CaptureRelativeTransform(ECSWorld& world, const Entity& entity, std::vector<AnimationPreviewBaseValue>& values);
	void CaptureClip(ECSWorld& world, const Entity& entity, const AnimationClipAsset& clip, bool relativeTransform,
		std::vector<AnimationPreviewBaseValue>& values);
	void Restore(ECSWorld& world, const Entity& entity, std::span<const AnimationPreviewBaseValue> values);
}
