#include "AnimationPlayerSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	AnimationPlayerSystem classMethods
//============================================================================
namespace {

	// グループ内のstate名からstateを引く
	const Engine::AnimationState* FindStateInGroup(const Engine::AnimationGroup& group, const std::string& name) {

		for (const Engine::AnimationState& state : group.states) {
			if (state.name == name) {
				return &state;
			}
		}
		return nullptr;
	}

	// 2つのbindingが同じプロパティを指すか
	bool SameBinding(const Engine::AnimationPropertyBinding& lhs, const Engine::AnimationPropertyBinding& rhs) {

		return lhs.componentName == rhs.componentName && lhs.propertyPath == rhs.propertyPath &&
			lhs.valueType == rhs.valueType;
	}

	// UseClipをクリップのloop設定から実際のwrapへ解決する
	Engine::AnimationWrapMode EffectiveWrap(Engine::AnimationWrapMode wrap, bool clipLoop) {

		if (wrap != Engine::AnimationWrapMode::UseClip) {
			return wrap;
		}
		return clipLoop ? Engine::AnimationWrapMode::Loop : Engine::AnimationWrapMode::Once;
	}

	// wrapModeに従ってクリップ内時刻を進める、非ループ終端でfinishedを立てる
	float AdvanceTime(float duration, Engine::AnimationWrapMode wrap,
		float time, int8_t& dir, float delta, bool& finished) {

		finished = false;
		const float dur = (std::max)(duration, 0.001f);
		switch (wrap) {
		case Engine::AnimationWrapMode::Once: {

			float t = time + delta;
			if (t >= dur) { t = dur; finished = true; } else if (t < 0.0f) { t = 0.0f; finished = true; }
			return t;
		}
		case Engine::AnimationWrapMode::PingPong: {

			// 端を越えた分を反射して往復させる
			float t = time + delta * static_cast<float>(dir);
			if (t > dur) { t = dur - (t - dur); dir = -1; } else if (t < 0.0f) { t = -t; dir = 1; }
			return (std::clamp)(t, 0.0f, dur);
		}
		case Engine::AnimationWrapMode::Loop:
		default: {

			float t = std::fmod(time + delta, dur);
			if (t < 0.0f) { t += dur; }
			return t;
		}
		}
	}

	// 1ループ完了時の回数チェック、指定回数に達したら終端保持で停止しfalseを返す
	bool CompleteLoopIteration(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur) {

		++rt.repeatCount;
		rt.phase = Engine::AnimationClipPhase::Play;
		rt.phaseTime = 0.0f;
		if (state.loopCount > 0 && rt.repeatCount >= state.loopCount) {
			rt.time = dur;
			rt.playing = false;
			rt.finished = true;
			return false;
		}
		rt.time = 0.0f;
		return true;
	}

	// Loopの進行、本編→繋ぎ補間→インターバルのフェーズを順に進める
	void AdvanceLoop(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur, float delta) {

		const bool bridgeOn = state.loopBridge.enabled && state.loopBridge.duration > 0.0f;
		const float bridgeDur = bridgeOn ? (std::max)(state.loopBridge.duration, 0.001f) : 0.0f;
		const float interval = (std::max)(state.interval, 0.0f);

		float remaining = delta;
		for (int guard = 0; remaining > 0.0f && guard < 16; ++guard) {

			if (rt.phase == Engine::AnimationClipPhase::Play) {

				const float room = dur - rt.time;
				if (remaining < room) { rt.time += remaining; return; }
				remaining -= room;
				rt.time = dur;
				if (bridgeOn) { rt.phase = Engine::AnimationClipPhase::Bridge; rt.phaseTime = 0.0f; } else if (interval > 0.0f) { rt.phase = Engine::AnimationClipPhase::Interval; rt.phaseTime = 0.0f; } else if (!CompleteLoopIteration(rt, state, dur)) { return; }
			} else if (rt.phase == Engine::AnimationClipPhase::Bridge) {

				const float room = bridgeDur - rt.phaseTime;
				if (remaining < room) { rt.phaseTime += remaining; return; }
				remaining -= room;
				// 繋ぎ補間は開始ポーズ(t=0)へ収束するので、以降はそこを保持する
				rt.time = 0.0f;
				if (interval > 0.0f) { rt.phase = Engine::AnimationClipPhase::Interval; rt.phaseTime = 0.0f; } else if (!CompleteLoopIteration(rt, state, dur)) { return; }
			} else {

				const float room = interval - rt.phaseTime;
				if (remaining < room) { rt.phaseTime += remaining; return; }
				remaining -= room;
				if (!CompleteLoopIteration(rt, state, dur)) { return; }
			}
		}
	}

	// PingPongの進行、1往復完了ごとにインターバルを挟む
	void AdvancePingPong(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur, float delta) {

		const float interval = (std::max)(state.interval, 0.0f);

		// インターバル待機中は消化してから逆再生を再開する
		if (rt.phase == Engine::AnimationClipPhase::Interval) {

			rt.phaseTime += delta;
			if (rt.phaseTime < interval) { return; }
			delta = rt.phaseTime - interval;
			rt.phase = Engine::AnimationClipPhase::Play;
			rt.phaseTime = 0.0f;
			rt.dir = 1;
			rt.time = 0.0f;
			if (delta <= 0.0f) { return; }
		}

		bool finished = false;
		const int8_t prevDir = rt.dir;
		rt.time = AdvanceTime(dur, Engine::AnimationWrapMode::PingPong, rt.time, rt.dir, delta, finished);

		// 開始位置(0)へ戻った(dir -1→+1)= 1往復完了
		if (prevDir == -1 && rt.dir == 1) {

			++rt.repeatCount;
			if (state.pingPongCount > 0 && rt.repeatCount >= state.pingPongCount) {
				rt.time = 0.0f;
				rt.playing = false;
				rt.finished = true;
				return;
			}
			if (interval > 0.0f) {
				rt.phase = Engine::AnimationClipPhase::Interval;
				rt.phaseTime = 0.0f;
				rt.time = 0.0f;
			}
		}
	}
}

const Engine::AnimationGroup* Engine::AnimationPlayerSystem::FindGroup(
	const AnimationPlayerComponent& player, const std::string& name) const {

	if (name.empty()) {
		return nullptr;
	}
	for (const AnimationGroup& group : player.groups) {
		if (group.name == name) {
			return &group;
		}
	}
	return nullptr;
}

void Engine::AnimationPlayerSystem::CaptureBaseValues(ECSWorld& world, const Entity& entity,
	const AnimationPlayerComponent& player, SystemContext& context, std::vector<AnimationPreviewBaseValue>& out) const {

	out.clear();
	if (!context.assetDatabase || !context.animationClipManager) {
		return;
	}

	// 同じbindingを重複させずに現在値を捕捉する
	const auto captureBase = [&](const std::string& componentName, const std::string& propertyPath,
		AnimationValueType valueType) {

			for (const AnimationPreviewBaseValue& base : out) {
				if (base.binding.componentName == componentName && base.binding.propertyPath == propertyPath) {
					return;
				}
			}
			const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, componentName, propertyPath, valueType);
			if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
				return;
			}
			AnimationPreviewBaseValue base{};
			base.binding.componentName = componentName;
			base.binding.propertyPath = propertyPath;
			base.binding.valueType = valueType;
			// material override未設定などは値が無いので、復元時に除去できるよう有無を記録する
			base.present = !desc->hasValue || desc->hasValue(world, entity);
			if (desc->getValue(world, entity, base.value)) {
				out.emplace_back(std::move(base));
			}
		};

	// 全グループの全クリップが触るプロパティの和集合を一度だけ捕捉する
	bool anyRelative = false;
	for (const AnimationGroup& group : player.groups) {
		for (const AnimationState& state : group.states) {

			const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
			if (!clip) {
				continue;
			}
			anyRelative |= state.relativeTransform;
			for (const AnimationCurveTrack& track : clip->curveTracks) {
				captureBase(track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
			}
		}
	}

	// 向き相対クリップは基準の位置/回転が必須なので、未アニメでも捕捉しておく
	if (anyRelative) {
		captureBase("Transform", "localPos", AnimationValueType::Vector3);
		captureBase("Transform", "localRotation", AnimationValueType::Quaternion);
		captureBase("Transform", "localPos2D", AnimationValueType::Vector2);
		captureBase("Transform", "localRotationZ", AnimationValueType::Float);
	}
}

void Engine::AnimationPlayerSystem::RestoreBaseValues(ECSWorld& world, const Entity& entity,
	const std::vector<AnimationPreviewBaseValue>& base) const {

	for (const AnimationPreviewBaseValue& value : base) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, value.binding.componentName, value.binding.propertyPath, value.binding.valueType);
		if (!desc || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}
		// Preview前に値が有ったものは戻し、無かったものは除去して既定へ戻す
		if (value.present) {
			if (desc->setValue) {
				desc->setValue(world, entity, value.value);
			}
		} else if (desc->clearValue) {
			desc->clearValue(world, entity);
		}
	}
}

void Engine::AnimationPlayerSystem::BeginGroup(AnimationPlayerComponent& player,
	const std::string& groupName, float fadeDuration) const {

	const AnimationGroup* group = FindGroup(player, groupName);
	if (!group) {
		return;
	}

	// クロスフェードは別グループを再生中の時だけ有効にする、現在の再生をfromへ退避する
	if (fadeDuration > 0.0f && player.runtimePlaying && !player.runtimeCurrentGroup.empty() &&
		player.runtimeCurrentGroup != groupName) {

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

void Engine::AnimationPlayerSystem::AdvanceClip(AnimationPlayerComponent& player,
	const AnimationGroup& group, AnimationClipRuntime& clipRt, SystemContext& context,
	std::vector<AnimationEvent>* firedOut) const {

	const AnimationState* state = FindStateInGroup(group, clipRt.stateName);
	if (!state) {
		return;
	}
	const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state->clip);
	if (!clip) {
		return;
	}

	const float baseDelta = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;

	// 開始遅延を消化する、経過するまでは再生しない
	if (!clipRt.started) {

		clipRt.delayRemaining -= baseDelta;
		if (clipRt.delayRemaining > 0.0f) {
			return;
		}
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

	// このフレームで跨いだイベント検出のため、進める前の状態を控える
	const float beforeTime = clipRt.time;
	const int8_t beforeDir = clipRt.dir;
	const AnimationClipPhase beforePhase = clipRt.phase;
	const int32_t beforeRepeat = clipRt.repeatCount;

	// ループ/往復はフェーズ機械で繋ぎ補間とインターバルを扱う、それ以外は素直に進める
	if (curWrap == AnimationWrapMode::Loop) {

		AdvanceLoop(clipRt, *state, dur, curDelta);
	} else if (curWrap == AnimationWrapMode::PingPong) {

		AdvancePingPong(clipRt, *state, dur, curDelta);
	} else {

		bool finished = false;
		clipRt.time = AdvanceTime(dur, curWrap, clipRt.time, clipRt.dir, curDelta, finished);
		if (finished) {
			clipRt.playing = false;
			clipRt.finished = true;
		}
	}

	// Play中のみ、本編再生フェーズで跨いだイベントを集める
	if (firedOut && context.mode == WorldMode::Play && !clip->events.empty()) {
		CollectClipEvents(*clip, curWrap, beforeTime, beforeDir, beforePhase, beforeRepeat, clipRt, dur, *firedOut);
	}
}

void Engine::AnimationPlayerSystem::CollectClipEvents(const AnimationClipAsset& clip, AnimationWrapMode wrap,
	float beforeTime, int8_t beforeDir, AnimationClipPhase beforePhase, int32_t beforeRepeat,
	const AnimationClipRuntime& clipRt, float dur, std::vector<AnimationEvent>& firedOut) const {

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

void Engine::AnimationPlayerSystem::EvaluateGroupClips(ECSWorld& world, const Entity& entity,
	const AnimationGroup& group, const std::vector<AnimationClipRuntime>& clips,
	std::span<const AnimationPreviewBaseValue> baseStore, SystemContext& context,
	std::vector<AnimationEvaluatedValue>& outValues) const {

	outValues.clear();

	// 開始済みクリップの評価値を集める、遅延中のクリップは寄与しない
	std::vector<AnimationEvaluatedValue> gathered;
	std::vector<AnimationEvaluatedValue> clipValues;
	for (const AnimationClipRuntime& clipRt : clips) {

		if (!clipRt.started) {
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
			endTime.clipTime = clip->duration;
			AnimationResolvedTime startTime{};
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
			gathered.emplace_back(std::move(value));
		}
	}

	// 同一プロパティを2つ以上のクリップが触っていたら競合とみなし、そのプロパティは採用しない
	for (size_t i = 0; i < gathered.size(); ++i) {

		int32_t contributors = 0;
		for (size_t j = 0; j < gathered.size(); ++j) {
			if (SameBinding(gathered[i].binding, gathered[j].binding)) {
				++contributors;
			}
		}
		if (contributors == 1) {
			outValues.emplace_back(gathered[i]);
		}
	}
}

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
		UpdatePlayer(world, entity, player, context);
		});
}
