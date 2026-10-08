#include "AnimationClipEditSession.h"
#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>

// c++
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipEditSession
//============================================================================

void AnimationClipEditSession::UpdatePreviewPlayback(const EditorToolContext& context, float deltaTime) {

	if (!previewPlaying_ || !hasClip_) {
		return;
	}

	if (deltaTime <= 0.0f) {
		deltaTime = context.toolContext.deltaTime;
	}

	// 再生時間を進める
	previewTime_ += deltaTime * previewSpeed_;
	// クリップデータから終了時間を取得
	float playbackDuration = AnimationClipEvaluator::GetPlaybackDuration(clip_);
	if (playbackDuration <= previewTime_) {
		// Loop時はBridge範囲も含めた再生長で折り返す
		if (clip_.loop && 0.0f < clip_.duration) {

			previewTime_ = std::fmod(previewTime_, playbackDuration);
		}
		// ループしない場合はプレビューを停止させる
		else {

			previewTime_ = playbackDuration;
			previewPlaying_ = false;
		}
	}

	SyncCurveStateTime();
	ApplyPreviewAtCurrentTime(context, false);
}

void AnimationClipEditSession::ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive) {

	if (!hasClip_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	if (previewActive_ && (previewWorld_ != world || previewEntity_ != entity)) {
		EndPreviewAndRestore();
	}
	// 時刻移動でも復元用の開始値を保持する
	if (!previewActive_) {
		BeginPreview(context);
	}
	if (!previewActive_) {
		return;
	}

	AnimationClipEvaluator::ApplyClip(*world, entity, clip_, previewTime_, previewBaseValues_);
	if (!keepActive && !previewPlaying_) {
		EndPreviewAndRestore(context);
	} else {
		// 外部編集の検出に使う適用値を保持する
		CaptureLastAppliedValues(*world, entity);
	}
}

void AnimationClipEditSession::BeginPreview(const EditorToolContext& context) {

	if (previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || !hasClip_) {
		return;
	}

	// Target Entityへ直接値を書き込むため、開始時の値を先に退避しておく
	previewBaseValues_.clear();
	CachePreviewBaseValues(*world, entity);
	previewWorld_ = world;
	previewWorldLifetime_ = world->GetLifetime();
	previewEntity_ = entity;
	previewActive_ = true;
}

void AnimationClipEditSession::EndPreviewAndRestore([[maybe_unused]] const EditorToolContext& context) {

	EndPreviewAndRestore();
}

void AnimationClipEditSession::EndPreviewAndRestore() {

	// 開始したWorldが生存している間だけ元の値へ戻す
	const auto lifetime = previewWorldLifetime_.lock();
	if (previewActive_ && lifetime && lifetime->IsAlive() && previewWorld_->IsAlive(previewEntity_)) {

		RestorePreviewBaseValues(*previewWorld_, previewEntity_);
	}
	previewWorld_ = nullptr;
	previewWorldLifetime_.reset();
	previewEntity_ = {};
	previewBaseValues_.clear();
	lastAppliedValues_.clear();
	previewActive_ = false;
	previewPlaying_ = false;
}

void AnimationClipEditSession::CachePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	// Clipと相対移動が使う開始値をまとめて保持する
	AnimationPropertySnapshot::CaptureClip(world, entity, clip_, clip_.relativeTransform, previewBaseValues_);
}

void AnimationClipEditSession::RestorePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	AnimationPropertySnapshot::Restore(world, entity, previewBaseValues_);
}

void AnimationClipEditSession::RestoreAndDropPreviewBaseValue(
	const EditorToolContext& context, const AnimationPropertyBinding& binding) {

	if (!previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	// 一括Propertyは個別の復元値もまとめて戻す
	std::vector<AnimationPropertyBinding> affected{binding};
	const auto descriptor = AnimationPropertyRegistry::GetInstance().ResolveProperty(
		*world, entity, binding.componentName, binding.propertyPath, binding.valueType);
	if (descriptor && descriptor->snapshotBindings) {

		auto expanded = descriptor->snapshotBindings(*world, entity);
		affected.insert(affected.end(), expanded.begin(), expanded.end());
	}
	for (auto it = previewBaseValues_.begin(); it != previewBaseValues_.end();) {

		const bool matches = std::any_of(affected.begin(), affected.end(),
			[&](const auto& target) { return AnimationValueOperations::SameBinding(it->binding, target); });
		if (!matches) {
			++it;
			continue;
		}
		AnimationPropertySnapshot::Restore(*world, entity, std::span<const AnimationPreviewBaseValue>(&*it, 1));
		it = previewBaseValues_.erase(it);
	}
}

void AnimationClipEditSession::CaptureLastAppliedValues(ECSWorld& world, const Entity& entity) {

	// 最後に適用した値をPropertyごとに保持する
	lastAppliedValues_.clear();
	for (const AnimationPreviewBaseValue& base : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, base.binding.componentName, base.binding.propertyPath, base.binding.valueType);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}
		AnimationPreviewBaseValue applied{};
		applied.binding = base.binding;
		if (desc->getValue(world, entity, applied.value)) {
			lastAppliedValues_.emplace_back(std::move(applied));
		}
	}
}

void AnimationClipEditSession::SyncPreviewBaseFromEntityEdits(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || previewBaseValues_.empty()) {
		return;
	}

	const auto resolve = [&](const AnimationPropertyBinding& binding) {
		return AnimationPropertyRegistry::GetInstance().ResolveProperty(
			*world, entity, binding.componentName, binding.propertyPath, binding.valueType);
	};
	// 編集軸の判定に使うTrackを探す
	const auto findTrack = [&](const AnimationPropertyBinding& binding) -> const AnimationCurveTrack* {
		for (const AnimationCurveTrack& track : clip_.curveTracks) {
			if (track.binding.componentName == binding.componentName && track.binding.propertyPath == binding.propertyPath &&
				track.binding.valueType == binding.valueType) {
				return &track;
			}
		}
		return nullptr;
	};

	if (previewActive_) {

		// 最終適用値から変わったPropertyを集める
		std::vector<AnimationPreviewBaseValue> edited{};
		for (const AnimationPreviewBaseValue& applied : lastAppliedValues_) {

			const std::optional<AnimationPropertyDescriptor> desc = resolve(applied.binding);
			if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(*world, entity)) {
				continue;
			}
			AnimationPropertyValue current{};
			if (!desc->getValue(*world, entity, current) || ApproxEqualValue(current, applied.value)) {
				continue;
			}
			AnimationPreviewBaseValue edit{};
			edit.binding = applied.binding;
			edit.value = current;
			edit.present = !desc->hasValue || desc->hasValue(*world, entity);
			edited.emplace_back(std::move(edit));
		}
		if (edited.empty()) {
			return;
		}

		// 外部編集を検出したらプレビューを復元する
		const auto captured = previewBaseValues_;
		EndPreviewAndRestore(context);
		previewBaseValues_ = captured;
		// 外部編集値を新しい基準値へ取り込む
		for (const AnimationPreviewBaseValue& edit : edited) {

			AnimationPreviewBaseValue* baseEntry = nullptr;
			for (AnimationPreviewBaseValue& base : previewBaseValues_) {
				if (base.binding.componentName == edit.binding.componentName &&
					base.binding.propertyPath == edit.binding.propertyPath &&
					base.binding.valueType == edit.binding.valueType) {
					baseEntry = &base;
					break;
				}
			}
			// 未使用の軸だけ外部編集値を採用する
			const AnimationCurveTrack* track = findTrack(edit.binding);
			const AnimationPropertyValue merged =
				(baseEntry && track) ? MergeEditedBaseValue(*track, edit.value, baseEntry->value) : edit.value;

			const std::optional<AnimationPropertyDescriptor> desc = resolve(edit.binding);
			if (desc && desc->setValue) {
				desc->setValue(*world, entity, merged);
			}
			if (baseEntry) {
				baseEntry->value = merged;
				baseEntry->present = edit.present;
			}
		}
		return;
	}

	// 停止中: baseとズレていたらユーザー編集なのでbaseを更新する
	for (AnimationPreviewBaseValue& base : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = resolve(base.binding);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(*world, entity)) {
			continue;
		}
		AnimationPropertyValue current{};
		if (!desc->getValue(*world, entity, current) || ApproxEqualValue(current, base.value)) {
			continue;
		}
		// キーのあるチャネルはbaseを保ち、キーの無いチャネルだけ編集値を採用する
		const AnimationCurveTrack* track = findTrack(base.binding);
		base.value = track ? MergeEditedBaseValue(*track, current, base.value) : current;
		base.present = !desc->hasValue || desc->hasValue(*world, entity);
	}
}

void AnimationClipEditSession::UpdateExternalEdits(const EditorToolContext& context) {

	if (previewActive_ && previewWorld_ != context.GetWorld()) {

		EndPreviewAndRestore();
		return;
	}
	if (context.panelContext && context.panelContext->editorState) {

		const EditorState& editorState = *context.panelContext->editorState;
		const size_t undoCount = editorState.commandHistory.GetUndoCount();
		const size_t redoCount = editorState.commandHistory.GetRedoCount();
		if (editorState.useSceneGizmo || undoCount != lastUndoCount_ || redoCount != lastRedoCount_) {
			SyncPreviewBaseFromEntityEdits(context);
		}
		lastUndoCount_ = undoCount;
		lastRedoCount_ = redoCount;
	}
}

void AnimationClipEditSession::SetPreviewTarget(const EditorToolContext& context, const UUID& nextTargetUUID) {

	// 旧対象の値を復元してから新対象へ現在時刻の値を適用する
	EndPreviewAndRestore(context);
	previewBaseValues_.clear();
	targetEntityUUID_ = nextTargetUUID;
	previewTime_ = hasClip_ ? (std::clamp)(previewTime_, 0.0f, clip_.duration) : 0.0f;
	curveState_.currentTime = previewTime_;
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::ClearPreviewTarget(const EditorToolContext& context) {

	// 編集前の値を復元してから対象を解除する
	EndPreviewAndRestore(context);
	previewBaseValues_.clear();
	targetEntityUUID_ = {};
}

void AnimationClipEditSession::TogglePreviewPlayback(const EditorToolContext& context) {

	if (previewPlaying_) {
		previewPlaying_ = false;
	} else {
		// 終端でPlayした時は、AnimationClipの先頭から再生し直す
		if (clip_.duration <= previewTime_) {
			previewTime_ = 0.0f;
			curveState_.currentTime = previewTime_;
		}
		BeginPreview(context);
		previewPlaying_ = previewActive_;
		ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipEditSession::StopPreviewPlayback(const EditorToolContext& context) {

	// 元の値を復元して再生時刻を先頭へ戻す
	EndPreviewAndRestore(context);
	previewTime_ = 0.0f;
	curveState_.currentTime = previewTime_;
}
