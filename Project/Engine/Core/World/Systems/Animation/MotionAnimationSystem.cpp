#include "MotionAnimationSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/Animation/Timeline/TimelineControl.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Engine;

namespace {

	// 対象の現在位置を取り込む
	bool CaptureTarget(ECSWorld& world, Entity entity, MotionAnimationRecord& record) {

		auto& runtime = record.runtime;
		const auto& settings = record.settings;
		if (settings.mode != MotionAnimationMode::ApproachTarget &&
			(!settings.applyRotation || settings.rotation != MotionAnimationRotation::LookAtTarget)) return true;
		const Entity target = runtime.hasTargetOverride ? runtime.targetOverride : settings.target.Resolve(world, entity);
		ResolvedWorldTransform pose;
		if (world.IsPendingDestroy(target) || !TransformWorldUtility::ResolveWorldTransform(world, target, pose, true)) {
			runtime.diagnostic = "移動・注視先のオブジェクトが見つかりません";
			runtime.playing = false;
			return false;
		}
		runtime.targetPosition = pose.matrix.GetTranslationValue();
		return true;
	}

	// 参照先と距離評価用の経路を取り直す
	bool CaptureResources(ECSWorld& world, Entity entity, MotionAnimationRecord& record) {

		auto& runtime = record.runtime;
		ResolvedWorldTransform pose;
		if (!CaptureTarget(world, entity, record)) return false;
		if (record.settings.mode == MotionAnimationMode::Spline) {
			const Entity source = runtime.hasSplineOverride ? runtime.splineOverride : record.settings.spline.Resolve(world, entity);
			const auto* spline = TryGetSpline(world, source);
			if (!spline || world.IsPendingDestroy(source) || !TransformWorldUtility::ResolveWorldTransform(world, source, pose, true)) {
				runtime.diagnostic = "Splineを持つオブジェクトを設定してください";
				runtime.playing = false;
				return false;
			}
			runtime.splineSamples = BuildSplineSamples(*spline, pose.matrix);
			if (runtime.splineSamples.empty()) {
				runtime.diagnostic = "Splineの経路が空です";
				runtime.playing = false;
				return false;
			}
		}
		runtime.resourcesDirty = false;
		return true;
	}

	// 開始姿勢と編集プレビューの復元位置を記録する
	bool CaptureStart(ECSWorld& world, Entity entity, MotionAnimationRecord& record) {

		auto& runtime = record.runtime;
		ResolvedWorldTransform pose;
		auto* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform || !TransformWorldUtility::ResolveWorldTransform(world, entity, pose)) return false;
		if (!runtime.initialized) {
			runtime.originalLocalPosition = transform->localPos;
			runtime.originalLocalRotation = transform->localRotation;
		}
		runtime.origin = pose.matrix.GetTranslationValue();
		runtime.originRotation = pose.rotation;
		runtime.position = runtime.origin;
		runtime.rotation = runtime.originRotation;
		runtime.deltaPosition = {};
		runtime.deltaRotation = Quaternion::Identity();
		runtime.time = 0;
		runtime.cycles = 0;
		runtime.finished = false;
		runtime.paused = false;
		runtime.initialized = true;
		runtime.pendingCycles = runtime.pendingCompletions = 0;
		runtime.pendingSegments.clear();
		runtime.splineSamples.clear();
		runtime.frozenTarget = false;
		runtime.diagnostic.clear();
		return CaptureResources(world, entity, record);
	}

	// 既存アニメーションが所有するTransformの項目を調べる
	void FindConflicts(ECSWorld& world, Entity entity, SystemContext& context, bool& position, bool& rotation) {

		const auto check = [&](const AnimationPropertyBinding& binding) {
			if (binding.componentName != "Transform") return;
			position |= binding.propertyPath.starts_with("localPos");
			rotation |= binding.propertyPath.starts_with("localRotation");
		};
		world.ForEach<SimpleAnimationComponent>([&](Entity owner, const SimpleAnimationComponent& simple) {
			if (!simple.enabled || !simple.autoApply || !IsEntityActiveInHierarchy(world, owner) ||
				(context.mode == WorldMode::Edit && !simple.playInEditMode) ||
				(!simple.runtimePlaying && !simple.runtimePaused && !simple.runtimeApplied)) return;
			const Entity target = simple.runtimeTargetOverride ? simple.runtimeTarget :
				(simple.target.localFileID ? simple.target.Resolve(world, owner) : owner);
			if (target == entity) check(simple.binding);
		});
		const auto* clip = world.TryGetComponent<AnimationClipPlayerComponent>(entity);
		if (clip && clip->enabled && (context.mode != WorldMode::Edit || clip->playInEditMode) &&
			(clip->runtimePlaying || clip->runtimePaused || clip->runtimeApplied) && context.assetDatabase && context.animationClipManager) {
			if (const auto* asset = ResolveAnimationClipAsset(world, *context.animationClipManager, *context.assetDatabase, clip->clip))
				for (const auto& track : asset->curveTracks) check(track.binding);
		}
	}

	// 親の継承設定を反映してワールド姿勢を適用する
	bool ApplyPose(ECSWorld& world, Entity entity, Vector3 position, Quaternion rotation, bool applyPosition, bool applyRotation) {

		return TransformWorldUtility::ApplyWorldPose(world, entity, position, rotation, applyPosition, applyRotation);
	}

	// 競合していない項目だけを適用する
	void ApplyAnimationPose(ECSWorld& world, Entity entity, MotionAnimationRecord& record, SystemContext& context,
		Vector3 position, Quaternion rotation) {

		const auto& settings = record.settings;
		auto& runtime = record.runtime;
		bool conflictPosition = false, conflictRotation = false;
		FindConflicts(world, entity, context, conflictPosition, conflictRotation);
		conflictPosition |= IsTimelineControlled(world, entity, "Transform", "localPos");
		conflictRotation |= IsTimelineControlled(world, entity, "Transform", "localRotation");
		if ((settings.applyPosition && conflictPosition) || (settings.applyRotation && conflictRotation))
			runtime.diagnostic = "他のアニメーションと適用先が競合しています";
		if (!ApplyPose(world, entity, position, rotation,
			settings.applyPosition && !conflictPosition, settings.applyRotation && !conflictRotation))
			runtime.diagnostic = "親のTransformを逆変換できません";
		else {
			runtime.appliedPosition |= settings.applyPosition && !conflictPosition;
			runtime.appliedRotation |= settings.applyRotation && !conflictRotation;
		}
	}

}

void Engine::RestoreMotionAnimationPreview(ECSWorld& world, Entity entity, MotionAnimationRuntime& runtime) {

	if (!runtime.edit || !runtime.initialized) return;
	if (auto* transform = world.TryGetComponent<TransformComponent>(entity)) {
		if (runtime.appliedPosition) transform->localPos = runtime.originalLocalPosition;
		if (runtime.appliedRotation) transform->localRotation = runtime.originalLocalRotation;
		MarkTransformSubtreeDirty(world, entity);
	}
	runtime = {};
}

void MotionAnimationSystem::RestoreEditPreviews(ECSWorld& world) {

	world.ForEach<MotionAnimationComponent>([&](Entity entity, MotionAnimationComponent&) {
		if (auto* record = TryGetMotionAnimation(world, entity)) RestoreMotionAnimationPreview(world, entity, record->runtime);
	});
}

void MotionAnimationSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditPreviews(world);
}

void MotionAnimationSystem::Update(ECSWorld& world, SystemContext& context) {

	world.ForEach<MotionAnimationComponent>([&](Entity entity, MotionAnimationComponent&) {
		auto* record = TryGetMotionAnimation(world, entity);
		if (!record || context.IsUpdateInterrupted()) return;
		if (record->settings.autoApply &&
			((record->settings.applyPosition && IsTimelineControlled(world, entity, "Transform", "localPos")) ||
			 (record->settings.applyRotation && IsTimelineControlled(world, entity, "Transform", "localRotation")))) {
			record->runtime.deltaPosition = {};
			record->runtime.deltaRotation = Quaternion::Identity();
			return;
		}
		if (!record->settings.enabled || !IsEntityActiveInHierarchy(world, entity) ||
			(context.mode == WorldMode::Edit && !record->settings.playInEditMode)) {
			RestoreMotionAnimationPreview(world, entity, record->runtime);
			return;
		}
		UpdateMotion(world, entity, *record, context);
	});
}

void MotionAnimationSystem::UpdateMotion(ECSWorld& world, Entity entity, MotionAnimationRecord& record, SystemContext& context) {

	auto& runtime = record.runtime;
	const auto& settings = record.settings;
	runtime.deltaPosition = {};
	runtime.deltaRotation = Quaternion::Identity();
	runtime.edit = context.mode == WorldMode::Edit;
	const int32_t request = std::exchange(runtime.request, 0);
	bool started = false;
	if (!runtime.initialized || request == 1 || request == 7) {
		const bool playing = request == 1 || (request == 7 ? runtime.playing : settings.playOnStart);
		if (!CaptureStart(world, entity, record)) return;
		runtime.playing = playing;
		started = true;
	}
	if (request == 2) runtime.paused = true;
	if (request == 3) {
		runtime.paused = false;
		if (!runtime.finished) runtime.playing = true;
	}
	if (request == 4) {
		runtime.playing = false;
		runtime.paused = false;
	}
	if (request == 5) {
		if (settings.autoApply) ApplyAnimationPose(world, entity, record, context, runtime.origin, runtime.originRotation);
		runtime.position = runtime.origin;
		runtime.rotation = runtime.originRotation;
		runtime.time = 0;
		runtime.cycles = 0;
		runtime.playing = runtime.paused = runtime.finished = false;
		runtime.pendingSegments.clear();
		runtime.pendingCycles = runtime.pendingCompletions = 0;
		return;
	}
	if (request == 8) {
		runtime.frozenTarget = true;
	}
	if (request == 9) {
		runtime.frozenTarget = false;
		if (!CaptureTarget(world, entity, record)) return;
	}
	if (request == 6) {
		runtime.time = std::clamp(runtime.seekTime, 0.0, 1e12);
		started = true;
	}
	const auto timing = ResolveLocalTime(world, entity, settings.timeSource, TimeChannel::MotionAnimation,
		context.mode == WorldMode::Edit ? context.unscaledDeltaTime : context.deltaTime, context.unscaledDeltaTime);
	if (!timing.valid) {
		runtime.diagnostic = "時間倍率の参照先が見つかりません";
		return;
	}
	const bool advancing = !started && runtime.playing && !runtime.paused && timing.deltaTime > 0 && settings.speed > 0;
	if (!advancing && !started) return;
	if (runtime.resourcesDirty && !CaptureResources(world, entity, record)) return;
	if (!runtime.frozenTarget && (settings.mode == MotionAnimationMode::ApproachTarget ||
		(settings.applyRotation && settings.rotation == MotionAnimationRotation::LookAtTarget))) {
		const Entity target = runtime.hasTargetOverride ? runtime.targetOverride : settings.target.Resolve(world, entity);
		if (!world.TryGetComponentForBinding<TransformComponent>(target)) {
			runtime.diagnostic = "移動・注視先のオブジェクトが見つかりません";
			runtime.playing = false;
			return;
		}
	}
	if (settings.mode == MotionAnimationMode::Spline) {
		const Entity source = runtime.hasSplineOverride ? runtime.splineOverride : settings.spline.Resolve(world, entity);
		if (!world.TryGetComponentForBinding<SplineComponent>(source) || !world.TryGetComponentForBinding<TransformComponent>(source)) {
			runtime.diagnostic = "Splineの参照先が見つかりません";
			runtime.playing = false;
			return;
		}
	}
	if (settings.trackTarget && !runtime.frozenTarget && !CaptureTarget(world, entity, record)) return;
	runtime.diagnostic.clear();
	const double previous = runtime.time;
	if (advancing) runtime.time = (std::min)(1e12, runtime.time + timing.deltaTime * settings.speed);
	if (request != 6) AccumulateMotionAnimationEvents(settings, runtime, previous, runtime.time);
	if (!runtime.playing && request != 6 && request != 1 && request != 7) return;
	const auto sample = EvaluateMotionAnimation(settings, runtime, runtime.time);
	runtime.deltaPosition = sample.position - runtime.position;
	runtime.deltaRotation = (sample.rotation * Quaternion::Inverse(runtime.rotation)).Normalize();
	runtime.position = sample.position;
	runtime.rotation = sample.rotation;
	runtime.cycles = sample.cycles;
	runtime.finished = sample.finished;
	if (runtime.finished) {
		runtime.playing = false;
		runtime.paused = false;
	}
	if (settings.autoApply) ApplyAnimationPose(world, entity, record, context, runtime.position, runtime.rotation);
}
