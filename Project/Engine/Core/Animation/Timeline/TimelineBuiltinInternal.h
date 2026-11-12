#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineClipInstance.h"

namespace Engine {

	std::unique_ptr<ITimelineClipInstance> CreateTimelineAnimationClip(TimelineTrackKind kind, const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> CreateTimelinePropertyClip(TimelineTrackKind kind, const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineEffectClip(TimelineTrackKind kind, const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineCameraClip(const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineAudioClip(const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineTimeClip(const TimelineClip& clip);
}
