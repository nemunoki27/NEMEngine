#include "AnimationPlaybackRequests.h"

//============================================================================
//	include
//============================================================================
#include "AnimationGroupLookup.h"
#include "AnimationPlaybackTime.h"
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

void Engine::AnimationPlaybackRequests::PrepareDirectClip(AnimationPlayerComponent& player, SystemContext& context) {

	if (!player.runtimeDirectClipRequest) return;
	const AssetID clipID = player.runtimeDirectClipRequest;
	player.runtimeDirectClipRequest = {};
	const auto* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, clipID);
	if (!clip) {

		Logger::Output(LogType::Engine, spdlog::level::warn, "Animation Clipを再生できません ID={}", ToString(clipID));
		return;
	}
	// Controllerを変更せず、このComponentの実行状態へ追加する
	AnimationState state;
	state.name = "Clip";
	state.clip = clipID;
	state.relativeTransform = clip->relativeTransform;
	state.loopBridge = clip->loopBridge;
	player.runtimeDirectGroup = { "__NEM_DirectClip", { std::move(state) } };
	player.runtimePlayRequest = player.runtimeDirectGroup.name;
	player.runtimePlayFade = 0.0f;
	player.runtimeNormalizedFade = false;
}

void Engine::AnimationPlaybackRequests::RefreshActiveClips(const AnimationPlayerComponent& player, SystemContext& context) {

	// 新しいTrackへ書き込む前に復元値を捕捉できるようにする
	for (const auto& name : { player.runtimeCurrentGroup, player.runtimeFromGroup }) {

		const auto* group = AnimationGroupLookup::FindGroup(player, name);
		if (!group) continue;
		for (const auto& state : group->states) context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
	}
}

float Engine::AnimationPlaybackRequests::ResolveFadeDuration(const AnimationPlayerComponent& player, SystemContext& context) {

	if (!std::isfinite(player.runtimePlayFade) || player.runtimePlayFade <= 0.0f) return 0.0f;
	if (!player.runtimeNormalizedFade) return player.runtimePlayFade;
	const auto* group = AnimationGroupLookup::FindGroup(player, player.runtimeCurrentGroup);
	if (!group || group->states.empty()) return 0.0f;
	const auto& state = group->states.front();
	const auto* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
	const float speed = std::abs(player.globalSpeed * state.speed);
	// 停止中の状態には無限の遷移時間を作らない
	return clip && speed > 0.0f ? player.runtimePlayFade * clip->duration / speed : 0.0f;
}

bool Engine::AnimationPlaybackRequests::ApplyRequestedTime(AnimationPlayerComponent& player, SystemContext& context) {

	if (!player.runtimeSeekRequested) return false;
	player.runtimeSeekRequested = false;
	const float requested = player.runtimePlayTime;
	player.runtimePlayTime = -std::numeric_limits<float>::infinity();
	if (!std::isfinite(requested) || requested < 0.0f) return false;
	const auto* group = AnimationGroupLookup::FindGroup(player, player.runtimeCurrentGroup);
	if (!group) return false;
	for (auto& runtime : player.runtimeCurrentClips) {

		const auto* state = AnimationGroupLookup::FindStateInGroup(*group, runtime.stateName);
		const auto* clip = state ? context.animationClipManager->GetOrLoad(*context.assetDatabase, state->clip) : nullptr;
		if (!clip) continue;
		const bool playing = runtime.playing || !runtime.started;
		const float duration = (std::max)(clip->duration, 0.001f);
		const double normalized = player.runtimeNormalizedOffset ? requested : static_cast<double>(requested) / duration;
		const auto wrap = AnimationPlaybackTime::EffectiveWrap(state->wrapMode, clip->loop);
		double phase = normalized;
		if (wrap == AnimationWrapMode::Loop) phase = std::fmod(normalized, 1.0);
		if (wrap == AnimationWrapMode::PingPong) {

			phase = std::fmod(normalized, 2.0);
			runtime.dir = phase < 1.0 ? 1 : -1;
			if (phase > 1.0) phase = 2.0 - phase;
		}
		runtime.time = static_cast<float>(std::clamp(phase, 0.0, 1.0) * duration);
		runtime.normalizedTime = normalized;
		runtime.repeatCount = static_cast<int32_t>((std::min)(std::floor(normalized / (wrap == AnimationWrapMode::PingPong ? 2.0 : 1.0)),
			static_cast<double>((std::numeric_limits<int32_t>::max)())));
		runtime.phase = AnimationClipPhase::Play;
		runtime.phaseTime = 0.0f;
		runtime.delayRemaining = 0.0f;
		runtime.started = true;
		runtime.sampled = true;
		// 飛ばした範囲のEventは配送しない
		runtime.eventStartPending = false;
		runtime.finished = wrap == AnimationWrapMode::Once && normalized >= 1.0;
		runtime.playing = playing && !runtime.finished;
		runtime.terminalPosePending = true;
	}
	player.runtimeController.evaluatedState = player.runtimeCurrentGroup;
	player.runtimeController.previousNormalizedTime = player.runtimeCurrentClips.empty() ? 0.0f :
		static_cast<float>(player.runtimeCurrentClips.front().normalizedTime);
	return true;
}
