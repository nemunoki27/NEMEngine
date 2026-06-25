#include "AnimationPlayerSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	AnimationPlayerSystem classMethods
//============================================================================
namespace {

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
		case Engine::AnimationWrapMode::Once:
		case Engine::AnimationWrapMode::ClampForever: {

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
}

const Engine::AnimationState* Engine::AnimationPlayerSystem::FindState(
	const AnimationPlayerComponent& player, const std::string& name) const {

	if (name.empty()) {
		return nullptr;
	}
	for (const AnimationState& state : player.states) {
		if (state.name == name) {
			return &state;
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

	// 全stateのクリップが触るプロパティの和集合を一度だけ捕捉する
	bool anyRelative = false;
	for (const AnimationState& state : player.states) {

		const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
		if (!clip) {
			continue;
		}
		anyRelative |= clip->relativeTransform;
		for (const AnimationCurveTrack& track : clip->curveTracks) {
			captureBase(track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
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

void Engine::AnimationPlayerSystem::BeginState(AnimationPlayerComponent& player,
	const std::string& stateName, float fadeDuration) const {

	if (!FindState(player, stateName)) {
		return;
	}

	// クロスフェードは別stateを再生中の時だけ有効にする
	if (fadeDuration > 0.0f && player.runtimePlaying && !player.runtimeCurrent.empty() &&
		player.runtimeCurrent != stateName) {

		player.runtimeFrom = player.runtimeCurrent;
		player.runtimeFromTime = player.runtimeTime;
		player.runtimeFromDir = player.runtimeDir;
		player.runtimeInTransition = true;
		player.runtimeFade = 0.0f;
		player.runtimeFadeDuration = fadeDuration;
	} else {

		player.runtimeInTransition = false;
		player.runtimeFrom.clear();
	}
	player.runtimeCurrent = stateName;
	player.runtimeTime = 0.0f;
	player.runtimeDir = 1;
	player.runtimePlaying = true;
	player.runtimeFinished = false;
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

	// 停止要求の消費
	if (player.runtimeStopRequest) {
		player.runtimePlaying = false;
		player.runtimeStopRequest = false;
	}
	// 再生要求の消費、無ければ自動開始する、EditプレビューはplayOnStartに依らず常に再生する
	if (!player.runtimePlayRequest.empty()) {

		BeginState(player, player.runtimePlayRequest, player.runtimePlayFade);
		player.runtimePlayRequest.clear();
		player.runtimePlayFade = 0.0f;
		player.runtimeStarted = true;
	} else if (!player.runtimeStarted) {

		player.runtimeStarted = true;
		const bool autoPlay = isPlay ? player.playOnStart : true;
		if (autoPlay && !player.defaultState.empty()) {
			BeginState(player, player.defaultState, 0.0f);
		}
	}

	// 再生stateとクリップが無ければ何もしない
	const AnimationState* current = FindState(player, player.runtimeCurrent);
	if (!current) {
		return;
	}
	const AnimationClipAsset* currentClip = context.animationClipManager->GetOrLoad(*context.assetDatabase, current->clip);
	if (!currentClip) {
		return;
	}

	const float baseDelta = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;

	// 時間を進める、再生中のみ
	if (player.runtimePlaying) {

		bool finished = false;
		const float curDelta = baseDelta * player.globalSpeed * current->speed;
		const AnimationWrapMode curWrap = EffectiveWrap(current->wrapMode, currentClip->loop);
		player.runtimeTime = AdvanceTime(currentClip->duration, curWrap, player.runtimeTime, player.runtimeDir, curDelta, finished);

		// 遷移中は遷移元の時間とフェードも進める
		if (player.runtimeInTransition) {

			const AnimationState* from = FindState(player, player.runtimeFrom);
			const AnimationClipAsset* fromClip = from ?
				context.animationClipManager->GetOrLoad(*context.assetDatabase, from->clip) : nullptr;
			if (from && fromClip) {

				bool fromFinished = false;
				const float fromDelta = baseDelta * player.globalSpeed * from->speed;
				const AnimationWrapMode fromWrap = EffectiveWrap(from->wrapMode, fromClip->loop);
				player.runtimeFromTime = AdvanceTime(fromClip->duration, fromWrap,
					player.runtimeFromTime, player.runtimeFromDir, fromDelta, fromFinished);
			}
			player.runtimeFade += player.runtimeFadeDuration > 0.0f ? baseDelta / player.runtimeFadeDuration : 1.0f;
			if (player.runtimeFade >= 1.0f) {
				player.runtimeFade = 1.0f;
				player.runtimeInTransition = false;
				player.runtimeFrom.clear();
			}
		}
		// 非ループ終端で停止して終端ポーズを保持する
		if (finished) {
			player.runtimePlaying = false;
			player.runtimeFinished = true;
		}
	}

	// 評価して書き込む、遷移中はfromとcurrentをフェードで合成する
	AnimationResolvedTime curTime{};
	curTime.clipTime = player.runtimeTime;
	std::vector<AnimationEvaluatedValue> outValues;
	AnimationClipEvaluator::EvaluateClipValues(world, entity, *currentClip, curTime, *baseStore, outValues);

	if (player.runtimeInTransition) {

		const AnimationState* from = FindState(player, player.runtimeFrom);
		const AnimationClipAsset* fromClip = from ?
			context.animationClipManager->GetOrLoad(*context.assetDatabase, from->clip) : nullptr;
		if (fromClip) {

			AnimationResolvedTime fromTime{};
			fromTime.clipTime = player.runtimeFromTime;
			std::vector<AnimationEvaluatedValue> fromValues;
			AnimationClipEvaluator::EvaluateClipValues(world, entity, *fromClip, fromTime, *baseStore, fromValues);
			std::vector<AnimationEvaluatedValue> blended;
			AnimationClipEvaluator::BlendValues(fromValues, outValues, *baseStore, player.runtimeFade, blended);
			outValues = std::move(blended);
		}
	}

	// 向き相対クリップは合成後の値を基準姿勢を正面として相対化する
	if (currentClip->relativeTransform) {
		AnimationClipEvaluator::ComposeRelativeTransform(outValues, *baseStore);
	}
	AnimationClipEvaluator::WriteValues(world, entity, outValues);
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
