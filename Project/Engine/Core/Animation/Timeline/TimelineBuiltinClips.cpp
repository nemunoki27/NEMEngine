#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
// c++
#include <exception>

std::unique_ptr<Engine::ITimelineClipInstance> Engine::CreateBuiltinTimelineClip(
	const TimelineTrack& track, const TimelineClip& clip, std::string& diagnostic) {

	try {
		diagnostic.clear();
		switch (track.kind) {
		case TimelineTrackKind::SkinnedAnimation:
		case TimelineTrackKind::Motion: return CreateTimelineAnimationClip(track.kind, clip);
		case TimelineTrackKind::Property:
		case TimelineTrackKind::Activation:
		case TimelineTrackKind::Collision: return CreateTimelinePropertyClip(track.kind, clip);
		case TimelineTrackKind::Particle:
		case TimelineTrackKind::Prefab: return CreateTimelineEffectClip(track.kind, clip);
		case TimelineTrackKind::Camera: return CreateTimelineCameraClip(clip);
		case TimelineTrackKind::Audio: return CreateTimelineAudioClip(clip);
		case TimelineTrackKind::TimeScale: return CreateTimelineTimeClip(clip);
		default: diagnostic = "標準トラックではない種類です"; return {};
		}
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return {};
	}
}
