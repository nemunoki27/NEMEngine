#include "AnimationGroupPlayback.h"

//============================================================================
//	include
//============================================================================
#include "AnimationGroupLookup.h"
#include "AnimationPlaybackTime.h"
#include "AnimationEventCollection.h"
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationGroupPlayback {

	using namespace AnimationGroupLookup;
	using namespace AnimationPlaybackTime;
	using namespace AnimationEventCollection;

	void BeginGroup(AnimationPlayerComponent& player, const std::string& groupName, float fadeDuration) {

		const AnimationGroup* group = FindGroup(player, groupName);
		if (!group) {
			return;
		}

		// 再生中の姿勢を遷移元へ退避する
		if (fadeDuration > 0.0f && player.runtimePlaying && !player.runtimeCurrentGroup.empty()) {

			player.runtimeFromGroup = player.runtimeCurrentGroup;
			player.runtimeFromClips = player.runtimeCurrentClips;
			player.runtimeInTransition = true;
			player.runtimeFade = 0.0f;
			player.runtimeFadeDuration = fadeDuration;
		} else {

			player.runtimeInTransition = false;
			player.runtimeFromGroup.clear();
			player.runtimeFromClips.clear();
		}

		// 新グループのクリップランタイムを初期化する、開始遅延を持つクリップは待機状態にする
		player.runtimeCurrentGroup = groupName;
		player.runtimeCurrentClips.clear();
		player.runtimeCurrentClips.reserve(group->states.size());
		for (const AnimationState& state : group->states) {

			AnimationClipRuntime clipRt{};
			clipRt.stateName = state.name;
			clipRt.dir = 1;
			clipRt.delayRemaining = (std::max)(state.startDelay, 0.0f);
			clipRt.started = clipRt.delayRemaining <= 0.0f;
			clipRt.playing = clipRt.started;
			player.runtimeCurrentClips.emplace_back(std::move(clipRt));
		}
	}

	void AdvanceClip(AnimationPlayerComponent& player,
		const AnimationGroup& group, AnimationClipRuntime& clipRt, SystemContext& context,
		std::vector<AnimationEvent>* firedOut) {

		const AnimationState* state = FindStateInGroup(group, clipRt.stateName);
		if (!state) {
			return;
		}
		const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state->clip);
		if (!clip) {
			return;
		}

		float baseDelta = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;
		if (!std::isfinite(baseDelta) || baseDelta <= 0.0f) return;

		// 開始遅延を消化する、経過するまでは再生しない
		if (!clipRt.started) {

			if (baseDelta < clipRt.delayRemaining) {
				clipRt.delayRemaining -= baseDelta;
				return;
			}
			// 遅延を超えた時間だけを本編へ渡す
			baseDelta -= clipRt.delayRemaining;
			clipRt.delayRemaining = 0.0f;
			clipRt.started = true;
			clipRt.playing = true;
		}
		if (!clipRt.playing) {
			return;
		}

		const float curDelta = baseDelta * player.globalSpeed * state->speed;
		const AnimationWrapMode curWrap = EffectiveWrap(state->wrapMode, clip->loop);
		const float dur = (std::max)(clip->duration, 0.001f);

		if (!std::isfinite(curDelta) || curDelta == 0.0f) return;
		// 逆再生は終端から開始する
		if (!clipRt.sampled) {
			clipRt.time = curDelta < 0.0f ? dur : 0.0f;
			clipRt.sampled = true;
		}
		std::vector<Interval> intervals;
		auto* traversal = firedOut && context.mode == WorldMode::Play && !clip->events.empty() ? &intervals : nullptr;

		// ループ/往復はフェーズ機械で繋ぎ補間とインターバルを扱う、それ以外は素直に進める
		if (curWrap == AnimationWrapMode::Loop) {

			AdvanceLoop(clipRt, *state, dur, curDelta, traversal);
		} else if (curWrap == AnimationWrapMode::PingPong) {

			AdvancePingPong(clipRt, *state, dur, curDelta, traversal);
		} else {

			bool finished = false;
			const float beforeTime = clipRt.time;
			clipRt.time = AdvanceTime(dur, curWrap, clipRt.time, clipRt.dir, curDelta, finished);
			clipRt.normalizedTime += std::abs(static_cast<double>(clipRt.time - beforeTime)) / dur;
			RecordInterval(clipRt, beforeTime, clipRt.time, traversal);
			if (finished) {
				clipRt.playing = false;
				clipRt.finished = true;
			}
		}

		clipRt.terminalPosePending = clipRt.finished;

		// Play中のみ、本編再生フェーズで跨いだイベントを集める
		if (traversal) {
			CollectClipEvents(*clip, intervals, *firedOut);
		}
	}
}
