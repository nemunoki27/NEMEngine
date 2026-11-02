#include "AnimationClipPlayerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <cmath>
#include <stdexcept>

void Engine::ApplyAnimationClipPlayerSettings(const AnimationClipPlayerComponent& source, AnimationClipPlayerComponent& target) {

	target.clip = source.clip;
	target.enabled = source.enabled;
	target.playOnStart = source.playOnStart;
	target.playInEditMode = source.playInEditMode;
	target.unscaledTime = source.unscaledTime;
	target.speed = source.speed;
	target.startDelay = source.startDelay;
	target.interval = source.interval;
	target.wrapMode = source.wrapMode;
	target.repeatCount = source.repeatCount;
}

void Engine::to_json(nlohmann::json& out, const AnimationClipPlayerComponent& component) {

	out = {{"clip", ToString(component.clip)}, {"enabled", component.enabled}, {"playOnStart", component.playOnStart},
		{"playInEditMode", component.playInEditMode}, {"unscaledTime", component.unscaledTime}, {"speed", component.speed},
		{"startDelay", component.startDelay}, {"interval", component.interval}, {"repeatCount", component.repeatCount},
		{"wrapMode", EnumAdapter<AnimationWrapMode>::ToString(component.wrapMode)}};
}

void Engine::from_json(const nlohmann::json& in, AnimationClipPlayerComponent& component) {

	AnimationClipPlayerComponent candidate;
	candidate.clip = ParseAssetID(in, "clip");
	candidate.enabled = in.value("enabled", true);
	candidate.playOnStart = in.value("playOnStart", true);
	candidate.playInEditMode = in.value("playInEditMode", false);
	candidate.unscaledTime = in.value("unscaledTime", false);
	candidate.speed = in.value("speed", 1.0f);
	candidate.startDelay = in.value("startDelay", 0.0f);
	candidate.interval = in.value("interval", 0.0f);
	candidate.repeatCount = in.value("repeatCount", 0);
	const auto wrap = EnumAdapter<AnimationWrapMode>::FromString(in.value("wrapMode", "UseClip"));
	if (!wrap || !std::isfinite(candidate.speed) || candidate.speed < 0 ||
		!std::isfinite(candidate.startDelay) || candidate.startDelay < 0 ||
		!std::isfinite(candidate.interval) || candidate.interval < 0 || candidate.repeatCount < 0) {
		throw std::invalid_argument("AnimationClipPlayerの再生設定が不正です");
	}
	candidate.wrapMode = *wrap;
	ApplyAnimationClipPlayerSettings(candidate, component);
}
