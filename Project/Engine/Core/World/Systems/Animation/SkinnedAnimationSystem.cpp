#include "SkinnedAnimationSystem.h"

//============================================================================
//	include
//============================================================================
#include "SkeletonPoseEvaluator.h"
#include "SkeletonPlaybackTime.h"
#include "SkinnedAnimationSampling.h"
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::SkeletonPoseEvaluator;

//============================================================================
//	SkinnedAnimationSystem classMethods
//============================================================================

void Engine::SkinnedAnimationSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	world.ForEach<MeshRendererComponent, SkinnedAnimationComponent, SkinnedAnimationRuntimeComponent>(
		[&](const Entity& entity, [[maybe_unused]] MeshRendererComponent& renderer, SkinnedAnimationComponent& anim,
			[[maybe_unused]] SkinnedAnimationRuntimeComponent& runtimeComponent) {
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}
			// チャンク外の骨格データを解決する
			SkinnedAnimationRuntimeData* runtime = TryGetSkinnedAnimationRuntime(world, entity);
			if (!runtime) {
				return;
			}
			if (runtime->timelineControlled) return;
			bool meshChanged = false;
			const auto animationSet = PrepareSkinnedAnimation(world, entity, context, meshChanged);
			if (!animationSet) return;

			// 再生するクリップの名前を取得
			std::string desiredClip = ResolveSkinnedAnimationClip(*animationSet, anim.clip);
			bool clipChanged = false;
			if (!desiredClip.empty() && desiredClip != runtime->currentClip && !runtime->inTransition) {

				clipChanged = true;
				// Clip切替で前回の終了状態を解除する
				runtime->animationFinished = false;
				runtime->fromClip = runtime->currentClip;
				runtime->toClip = desiredClip;
				runtime->fromTime = runtime->time;
				runtime->toTime = anim.playbackSpeed < 0.0f ? animationSet->clips.at(desiredClip).duration : 0.0f;
				runtime->blendTime = 0.0f;
				runtime->inTransition = (anim.transitionDuration > 0.0f);
				if (!runtime->inTransition) {

					runtime->currentClip = desiredClip;
					runtime->time = runtime->toTime;
				}
			}

			// アニメーションの更新を行うか
			bool allowTimeAdvance = anim.enabled && (context.mode == WorldMode::Play || anim.playInEditMode);
			// Playはゲーム時間、Editは実時間で進める
			const float sourceDelta = ResolveLocalTime(world, entity, anim.timeSource, TimeChannel::SkinnedAnimation,
				context.mode == WorldMode::Play ? context.deltaTime : context.unscaledDeltaTime, context.unscaledDeltaTime).deltaTime;
			// フレーム時間を再生速度に応じてスケーリング
			float deltaTime = allowTimeAdvance && std::isfinite(sourceDelta) && std::isfinite(anim.playbackSpeed)
								  ? (std::max)(sourceDelta, 0.0f) * anim.playbackSpeed
								  : 0.0f;

			// 停止中や再生完了後の同じポーズは再計算しない
			const bool transitionAdvances = runtime->inTransition && deltaTime != 0.0f;
			const bool clipAdvances = !runtime->inTransition && deltaTime != 0.0f && (anim.loop || !runtime->animationFinished);
			const bool poseDirty = meshChanged || clipChanged || transitionAdvances || clipAdvances || !runtime->initialized ||
								   runtime->palette.empty();
			if (!poseDirty) {
				return;
			}

			// スケルトンをバインドポーズで初期化
			runtime->skeleton = runtime->bindSkeleton;
			// アニメーション遷移していないとき
			if (!runtime->inTransition) {

				const AnimationData& clip = animationSet->clips.at(runtime->currentClip);
				runtime->currentDuration = clip.duration;
				runtime->time = SkeletonPlaybackTime::Advance(
					runtime->time, clip.duration, deltaTime, anim.loop, runtime->repeatCount, runtime->animationFinished);

				// 現在再生中のアニメーションクリップをスケルトンに適用
				auto trackIt = animationSet->clipJointTracks.find(runtime->currentClip);
				if (trackIt != animationSet->clipJointTracks.end()) {

					ApplyClipToSkeleton(runtime->skeleton, trackIt->second, runtime->time);
				}
			}
			// アニメーション遷移中
			else {

				// 遷移元と遷移先のアニメーションクリップを取得
				const AnimationData& toClip = animationSet->clips.at(runtime->toClip);
				runtime->currentDuration = toClip.duration;

				// 遷移時間を進める
				runtime->blendTime += std::abs(deltaTime);
				int32_t ignoredRepeat = 0;
				bool ignoredFinished = false;
				runtime->fromTime = SkeletonPlaybackTime::Advance(runtime->fromTime,
					animationSet->clips.at(runtime->fromClip).duration, deltaTime, anim.loop, ignoredRepeat, ignoredFinished);
				runtime->toTime = SkeletonPlaybackTime::Advance(
					runtime->toTime, toClip.duration, deltaTime, anim.loop, runtime->repeatCount, runtime->animationFinished);
				// 遷移時間に対する経過時間の割合を計算
				float alpha = 0.0f < anim.transitionDuration ? runtime->blendTime / anim.transitionDuration : 1.0f;

				// 遷移元と遷移先の骨格ポーズを合成する
				auto fromTrackIt = animationSet->clipJointTracks.find(runtime->fromClip);
				auto toTrackIt = animationSet->clipJointTracks.find(runtime->toClip);
				if (fromTrackIt != animationSet->clipJointTracks.end() && toTrackIt != animationSet->clipJointTracks.end()) {

					BlendClipsToSkeleton(runtime->skeleton, fromTrackIt->second, runtime->fromTime, toTrackIt->second,
						runtime->toTime, std::clamp(alpha, 0.0f, 1.0f));
				}

				// 遷移完了後は次のClipへ再生状態を渡す
				if (1.0f <= alpha) {

					runtime->inTransition = false;
					runtime->currentClip = runtime->toClip;
					runtime->time = runtime->toTime;
				}
			}
			// スケルトンの階層を更新してGPU用のパレットを構築
			UpdateSkeletonHierarchy(runtime->skeleton);
			BuildPalette(runtime->skeleton, animationSet->skinCluster, runtime->palette);
			++runtime->poseGeneration;
		});
}
