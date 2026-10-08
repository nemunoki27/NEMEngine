#include "AnimationClipEvaluator.h"

// c++
#include <algorithm>
#include <cmath>

Engine::AnimationResolvedTime Engine::AnimationClipEvaluator::ResolveClipEvaluationTime(
	const AnimationClipAsset& clip, float playbackTime) {

	AnimationResolvedTime result{};
	const float duration = (std::max)(clip.duration, 0.001f);

	// 非Loopは範囲外をClampするだけ
	if (!clip.loop) {
		result.clipTime = (std::clamp)(playbackTime, 0.0f, duration);
		return result;
	}

	if (clip.loopBridge.enabled && 0.0f < clip.loopBridge.duration) {
		// Bridge有効時はduration + bridgeDurationを1周期として扱う
		const float bridgeDuration = (std::max)(clip.loopBridge.duration, 0.001f);
		const float period = duration + bridgeDuration;
		float localTime = std::fmod((std::max)(0.0f, playbackTime), period);
		if (localTime <= duration) {
			result.clipTime = localTime;
			return result;
		}
		result.clipTime = duration;
		result.inLoopBridge = true;
		result.bridgeT = (localTime - duration) / bridgeDuration;
		return result;
	}

	result.clipTime = std::fmod((std::max)(0.0f, playbackTime), duration);
	return result;
}

float Engine::AnimationClipEvaluator::BridgeInterp(float t, CurveInterpolationMode mode) {

	// LoopBridgeはハンドルを持たないので、BezierはLinear相当で扱う
	t = (std::clamp)(t, 0.0f, 1.0f);
	switch (mode) {
	case CurveInterpolationMode::Constant:
		return 0.0f;
	case CurveInterpolationMode::Spline:
	case CurveInterpolationMode::Squad:
		return t * t * (3.0f - 2.0f * t);
	case CurveInterpolationMode::Linear:
	case CurveInterpolationMode::Bezier:
	default:
		return t;
	}
}

float Engine::AnimationClipEvaluator::GetPlaybackDuration(const AnimationClipAsset& clip) {

	const float duration = (std::max)(clip.duration, 0.001f);
	return clip.loop && clip.loopBridge.enabled ? duration + (std::max)(clip.loopBridge.duration, 0.001f) : duration;
}
