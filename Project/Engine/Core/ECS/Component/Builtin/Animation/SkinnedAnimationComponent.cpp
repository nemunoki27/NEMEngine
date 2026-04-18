#include "SkinnedAnimationComponent.h"

//============================================================================
//	SkinnedAnimationComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, SkinnedAnimationComponent& component) {

	component.enabled = in.value("enabled", true);
	component.loop = in.value("loop", true);
	component.playInEditMode = in.value("playInEditMode", true);
	component.playbackSpeed = in.value("playbackSpeed", 1.0f);
	component.transitionDuration = in.value("transitionDuration", 0.15f);
	component.clip = in.value("clip", std::string("Default"));

	// ランタイムデータは初期化
	component.runtimeMesh = {};
	component.runtimeInitialized = false;
	component.runtimeCurrentClip.clear();
	component.runtimeFromClip.clear();
	component.runtimeToClip.clear();
	component.runtimeTime = 0.0f;
	component.runtimeFromTime = 0.0f;
	component.runtimeNextTime = 0.0f;
	component.runtimeBlendTime = 0.0f;
	component.runtimeInTransition = false;
	component.runtimeAnimationFinished = false;
	component.runtimeRepeatCount = 0;
	component.runtimeBindSkeleton = Skeleton{};
	component.runtimeSkeleton = Skeleton{};
	component.palette.clear();
	component.runtimeAvailableClips.clear();
}

void Engine::to_json(nlohmann::json& out, const SkinnedAnimationComponent& component) {

	out["enabled"] = component.enabled;
	out["loop"] = component.loop;
	out["playInEditMode"] = component.playInEditMode;
	out["playbackSpeed"] = component.playbackSpeed;
	out["transitionDuration"] = component.transitionDuration;
	out["clip"] = component.clip;
}