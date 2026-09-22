#include "AnimationPlayerSystem.h"

//============================================================================
//	include
//============================================================================
#include "AnimationGroupLookup.h"
#include "AnimationGroupPlayback.h"
#include "AnimationBaseValues.h"
#include "AnimationGroupEvaluation.h"
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::AnimationGroupLookup;
using namespace Engine::AnimationGroupPlayback;
using namespace Engine::AnimationBaseValues;
using namespace Engine::AnimationGroupEvaluation;

//============================================================================
//	AnimationPlayerSystem classMethods
//============================================================================

void Engine::AnimationPlayerSystem::UpdatePlayer(ECSWorld& world, const Entity& entity,
	AnimationPlayerComponent& player, SystemContext& context) {

	const bool isPlay = (context.mode == WorldMode::Play);
	const bool wantApply = player.enabled && (isPlay || player.playInEditMode);
	const UUID uuid = world.GetUUID(entity);

	// Editプレビューを止めた/無効化したら、system保持のbaseで元の見た目へ戻す
	if (!isPlay) {

		auto it = editPreviewBase_.find(uuid);
		if (!wantApply) {

			if (it != editPreviewBase_.end()) {
				RestoreBaseValues(world, entity, it->second);
				editPreviewBase_.erase(it);
			}
			// 次にONへ戻したとき頭から再生できるよう状態を落とす
			player.runtimeCurrentGroup.clear();
			player.runtimeCurrentClips.clear();
			player.runtimeFromGroup.clear();
			player.runtimeFromClips.clear();
			player.runtimeInTransition = false;
			player.runtimePlaying = false;
			player.runtimeStarted = false;
			return;
		}
	} else if (!wantApply) {
		return;
	}
	if (!context.assetDatabase || !context.animationClipManager) {
		return;
	}

	// baseはEditはsystem保持(commitで消えない)、Playはcomponentに置く
	std::vector<AnimationPreviewBaseValue>* baseStore = nullptr;
	if (isPlay) {

		baseStore = &player.runtimeBaseValues;
		if (!player.runtimeBaseCaptured) {
			CaptureBaseValues(world, entity, player, context, *baseStore);
			player.runtimeBaseCaptured = true;
		}
	} else {

		auto [it, inserted] = editPreviewBase_.try_emplace(uuid);
		baseStore = &it->second;
		// プレビュー開始フレームで、アニメ適用前の現在値を捕捉する
		if (inserted) {
			CaptureBaseValues(world, entity, player, context, *baseStore);
		}
	}

	// 停止要求の消費、現在グループの全クリップを停止する
	if (player.runtimeStopRequest) {
		for (AnimationClipRuntime& clipRt : player.runtimeCurrentClips) {
			clipRt.playing = false;
		}
		player.runtimeInTransition = false;
		player.runtimeFromClips.clear();
		player.runtimeFromGroup.clear();
		player.runtimeStopRequest = false;
	}
	// 再生要求の消費、無ければ自動開始する、EditプレビューはplayOnStartに依らず常に再生する
	if (!player.runtimePlayRequest.empty()) {

		// 向き相対の基準をPlayを呼んだ瞬間の姿勢にするため、要求時に捕捉し直す
		if (isPlay) {
			CaptureBaseValues(world, entity, player, context, *baseStore);
		}
		BeginGroup(player, player.runtimePlayRequest, player.runtimePlayFade);
		player.runtimePlayRequest.clear();
		player.runtimePlayFade = 0.0f;
		player.runtimeStarted = true;
	} else if (!player.runtimeStarted) {

		player.runtimeStarted = true;
		const bool autoPlay = isPlay ? player.playOnStart : true;
		if (autoPlay && !player.defaultGroup.empty()) {
			BeginGroup(player, player.defaultGroup, 0.0f);
		}
	}

	// 再生中グループが無ければ何もしない
	if (player.runtimeCurrentGroup.empty()) {
		return;
	}
	const AnimationGroup* currentGroup = FindGroup(player, player.runtimeCurrentGroup);
	if (!currentGroup) {
		return;
	}

	// 現在グループのクリップを進めて評価する、競合プロパティは除外される
	// 発火分は再入を避けるため一旦ためて、書き込み後にまとめて配送する
	std::vector<AnimationEvent> firedEvents;
	for (AnimationClipRuntime& clipRt : player.runtimeCurrentClips) {
		AdvanceClip(player, *currentGroup, clipRt, context, &firedEvents);
	}
	std::vector<AnimationEvaluatedValue> outValues;
	EvaluateGroupClips(world, entity, *currentGroup, player.runtimeCurrentClips, *baseStore, context, outValues);

	// クロスフェード中は遷移元グループも進めて評価し、fadeで合成する
	if (player.runtimeInTransition) {

		const AnimationGroup* fromGroup = FindGroup(player, player.runtimeFromGroup);
		if (fromGroup) {

			for (AnimationClipRuntime& clipRt : player.runtimeFromClips) {
				AdvanceClip(player, *fromGroup, clipRt, context, nullptr);
			}
			std::vector<AnimationEvaluatedValue> fromValues;
			EvaluateGroupClips(world, entity, *fromGroup, player.runtimeFromClips, *baseStore, context, fromValues);
			std::vector<AnimationEvaluatedValue> blended;
			AnimationClipEvaluator::BlendValues(fromValues, outValues, *baseStore, player.runtimeFade, blended);
			outValues = std::move(blended);
		}
		const float baseDelta = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;
		player.runtimeFade += player.runtimeFadeDuration > 0.0f ? baseDelta / player.runtimeFadeDuration : 1.0f;
		if (player.runtimeFade >= 1.0f) {
			player.runtimeFade = 1.0f;
			player.runtimeInTransition = false;
			player.runtimeFromGroup.clear();
			player.runtimeFromClips.clear();
		}
	}
	AnimationClipEvaluator::WriteValues(world, entity, outValues);

	// 書き込み後にイベントを配送する、ハンドラ内でPlay/Stopを呼んでも次フレーム消費で安全
	for (const AnimationEvent& event : firedEvents) {
		BehaviorSystem::DispatchAnimationEvent(world, context, entity,
			event.name, event.floatParam, event.intParam, event.stringParam);
	}

	// C#公開用ミラーを更新する、いずれかのクリップが再生中ならplaying、全て終端ならfinished
	player.runtimeCurrent = player.runtimeCurrentGroup;
	player.runtimePlaying = false;
	bool allFinished = true;
	int32_t maxRepeat = 0;
	for (const AnimationClipRuntime& clipRt : player.runtimeCurrentClips) {

		player.runtimePlaying = player.runtimePlaying || clipRt.playing;
		allFinished = allFinished && clipRt.finished;
		maxRepeat = (std::max)(maxRepeat, clipRt.repeatCount);
	}
	player.runtimeFinished = !player.runtimeCurrentClips.empty() && allFinished;
	player.runtimeRepeatCount = maxRepeat;
}

void Engine::AnimationPlayerSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	// World切り替えで、Editプレビューで適用した値を元へ戻してから破棄する
	for (const auto& [uuid, base] : editPreviewBase_) {

		const Entity entity = world.FindByUUID(uuid);
		if (world.IsAlive(entity)) {
			RestoreBaseValues(world, entity, base);
		}
	}
	editPreviewBase_.clear();
}

void Engine::AnimationPlayerSystem::Update(ECSWorld& world, SystemContext& context) {

	world.ForEach<AnimationPlayerComponent>([&](Entity entity, AnimationPlayerComponent& player) {
		if (!IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		UpdatePlayer(world, entity, player, context);
		});
}
