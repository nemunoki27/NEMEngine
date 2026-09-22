#include "AnimationEventCollection.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationEventCollection {

	void CollectClipEvents(const AnimationClipAsset& clip, AnimationWrapMode wrap,
		float beforeTime, int8_t beforeDir, AnimationClipPhase beforePhase, int32_t beforeRepeat,
		const AnimationClipRuntime& clipRt, float dur, std::vector<AnimationEvent>& firedOut) {

		// 半開区間 (lo, hi] を跨いだイベントを積む、上向きに通過した瞬間に発火する
		// 区間始点がクリップ先頭(0)のときは時刻0のイベントも含める(再生開始フレームやループ先頭で発火させる)
		const auto emit = [&](float lo, float hi) {
			if (hi <= lo) {
				return;
			}
			const bool includeStart = lo <= 0.0f;
			for (const AnimationEvent& event : clip.events) {
				const bool inRange = includeStart ? (0.0f <= event.time && event.time <= hi) : (lo < event.time && event.time <= hi);
				if (inRange) {
					firedOut.push_back(event);
				}
			}
			};

		if (wrap == AnimationWrapMode::Loop) {

			const bool cycled = clipRt.repeatCount != beforeRepeat;
			if (beforePhase == AnimationClipPhase::Play) {

				if (!cycled && clipRt.phase == AnimationClipPhase::Play) {
					emit(beforeTime, clipRt.time);
				} else {

					// 今周の終端まで再生した、繋ぎ補間/インターバルが0で同フレームに次周へ入ったら先頭側も見る
					emit(beforeTime, dur);
					if (cycled && clipRt.phase == AnimationClipPhase::Play) {
						emit(0.0f, clipRt.time);
					}
				}
			} else if (clipRt.phase == AnimationClipPhase::Play) {

				// 繋ぎ補間/インターバルを終えて次周の本編へ入った
				emit(0.0f, clipRt.time);
			}
		} else if (wrap == AnimationWrapMode::PingPong) {

			if (beforePhase == AnimationClipPhase::Play && clipRt.phase == AnimationClipPhase::Play) {

				if (beforeDir == clipRt.dir) {
					emit((std::min)(beforeTime, clipRt.time), (std::max)(beforeTime, clipRt.time));
				} else if (beforeDir == 1) {

					// 端で折り返した、行き(→dur)と戻り(dur→)の両方で発火する
					emit(beforeTime, dur);
					emit(clipRt.time, dur);
				} else {

					emit(0.0f, beforeTime);
					emit(0.0f, clipRt.time);
				}
			} else if (beforePhase == AnimationClipPhase::Interval && clipRt.phase == AnimationClipPhase::Play) {
				emit(0.0f, clipRt.time);
			}
		} else {

			// Once、単調前進
			emit(beforeTime, clipRt.time);
		}
	}
}
