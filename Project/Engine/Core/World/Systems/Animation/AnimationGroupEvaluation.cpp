#include "AnimationGroupEvaluation.h"

//============================================================================
//	include
//============================================================================
#include "AnimationGroupLookup.h"
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationGroupEvaluation {

	using namespace AnimationGroupLookup;

	void EvaluateGroupClips(ECSWorld& world, const Entity& entity,
		const AnimationGroup& group, const std::vector<AnimationClipRuntime>& clips,
		std::span<const AnimationPreviewBaseValue> baseStore, SystemContext& context,
		std::vector<AnimationEvaluatedValue>& outValues, bool holdFinished) {

		outValues.clear();

		// 開始済みクリップの評価値を集める、遅延中のクリップは寄与しない
		std::vector<AnimationContribution> gathered;
		std::vector<AnimationEvaluatedValue> clipValues;
		for (const AnimationClipRuntime& clipRt : clips) {

			if (!clipRt.started) {
				continue;
			}
			// 終了済みクリップは書き込みを止める、最終ポーズは書き込み済みで外部からの編集を妨げない
			if ((!clipRt.playing || clipRt.finished) && !clipRt.terminalPosePending && !(holdFinished && clipRt.finished)) {
				continue;
			}
			const AnimationState* state = FindStateInGroup(group, clipRt.stateName);
			if (!state) {
				continue;
			}
			const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state->clip);
			if (!clip) {
				continue;
			}

			// 繋ぎ補間中はend(duration)とstart(0)をstate側の補間で混ぜる、それ以外は現在時刻を評価する
			if (clipRt.phase == AnimationClipPhase::Bridge) {

				const float bridgeDur = (std::max)(state->loopBridge.duration, 0.001f);
				const float weight = AnimationClipEvaluator::BridgeInterp(clipRt.phaseTime / bridgeDur, state->loopBridge.interpolation);
				AnimationResolvedTime endTime{};
				endTime.clipTime = clipRt.dir > 0 ? clip->duration : 0.0f;
				AnimationResolvedTime startTime{};
				startTime.clipTime = clipRt.dir > 0 ? 0.0f : clip->duration;
				std::vector<AnimationEvaluatedValue> endValues;
				std::vector<AnimationEvaluatedValue> startValues;
				AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, endTime, baseStore, endValues);
				AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, startTime, baseStore, startValues);
				AnimationClipEvaluator::BlendValues(endValues, startValues, baseStore, weight, clipValues);
			} else {

				AnimationResolvedTime curTime{};
				curTime.clipTime = clipRt.time;
				AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, curTime, baseStore, clipValues);
			}

			// 向き相対はstate設定で判定する、相対でなければキーの無いチャネルは現在値を維持する
			if (state->relativeTransform) {

				AnimationResolvedTime neutralTime{};
				std::vector<AnimationEvaluatedValue> neutralValues;
				AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, neutralTime, baseStore, neutralValues);
				AnimationClipEvaluator::ComposeRelativeTransform(clipValues, baseStore, neutralValues);
			} else {

				AnimationClipEvaluator::PreserveUnkeyedChannels(world, entity, *clip, clipValues);
			}

			for (AnimationEvaluatedValue& value : clipValues) {
				gathered.push_back({ std::move(value), state->weight, state->priority, state->additive });
			}
		}

		// 別成分は共存させ、同じ成分はweightで混ぜる
		AnimationClipEvaluator::ComposeValues(gathered, baseStore, outValues);
	}
}
