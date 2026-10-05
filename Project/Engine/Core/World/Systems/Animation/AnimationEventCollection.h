#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPlaybackTime.h"

namespace Engine::AnimationEventCollection {

	void CollectClipEvents(const AnimationClipAsset& clip, std::span<const AnimationPlaybackTime::Interval> intervals,
		std::vector<AnimationEvent>& firedOut);
}
