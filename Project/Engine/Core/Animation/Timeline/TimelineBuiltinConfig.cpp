#include "TimelineBuiltinConfig.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>

nlohmann::json Engine::CreateDefaultTimelineClipData(TimelineTrackKind kind) {

	using nlohmann::json;
	switch (kind) {
	case TimelineTrackKind::SkinnedAnimation: return {{"clip", ""}, {"loop", false}};
	case TimelineTrackKind::Motion: return {{"settings", MotionAnimationSettings{}}, {"targetBinding", ""}, {"splineBinding", ""}};
	case TimelineTrackKind::Particle: return {{"oneShot", false}, {"randomSeed", 1}, {"stopEmittingOnEnd", true}, {"clearOnCancel", true}};
	case TimelineTrackKind::Prefab: return {{"asset", ""}, {"parentBinding", ""}, {"joint", ""},
		{"offset", Vector3{}.ToJson()}, {"rotation", Vector3{}.ToJson()}, {"scale", Vector3(1, 1, 1).ToJson()}, {"destroyOnEnd", true}};
	case TimelineTrackKind::Collision: return {{"debug", true}};
	case TimelineTrackKind::Camera: return {{"shotBinding", ""}, {"fov", 60.0f}, {"shake", Vector3{}.ToJson()}, {"shakeFrequency", 12.0f}, {"seed", 1}};
	case TimelineTrackKind::Audio: return {{"asset", ""}, {"volume", 1.0f}, {"loop", false}, {"pitch", 1.0f}, {"spatialBlend", 0.0f}};
	case TimelineTrackKind::Property: return {{"binding", {{"component", "Transform"}, {"property", "localPos"}, {"type", "Vector3"}}},
		{"from", Vector3{}.ToJson()}, {"to", Vector3{}.ToJson()}, {"asset", ""}, {"restoreOnEnd", true}};
	case TimelineTrackKind::Activation: return {{"active", true}};
	case TimelineTrackKind::TimeScale: return {{"scale", 0.25f}, {"global", false}, {"hitStop", false}, {"holdSeconds", 0.1f}};
	default: return json::object();
	}
}

std::string_view Engine::GetTimelineTrackRequiredComponent(TimelineTrackKind kind) {

	switch (kind) {
	case TimelineTrackKind::SkinnedAnimation: return "SkinnedAnimation";
	case TimelineTrackKind::Motion: return "Transform";
	case TimelineTrackKind::Particle: return "ParticleSystem";
	case TimelineTrackKind::Collision: return "Collision";
	case TimelineTrackKind::Camera: return "Camera";
	case TimelineTrackKind::Audio: return "AudioSource";
	case TimelineTrackKind::Activation: return "SceneObject";
	case TimelineTrackKind::TimeScale: return "TimeScale";
	default: return {};
	}
}
