#include "SkinnedAnimationSystem.h"

//============================================================================
//	include
//============================================================================
#include "SkeletonPoseEvaluator.h"
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
		[&](const Entity& entity, MeshRendererComponent& renderer,
			SkinnedAnimationComponent& anim, [[maybe_unused]] SkinnedAnimationRuntimeComponent& runtimeComponent) {

			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}
			// 重いスケルトンとパレットはチャンク外Storageから一度だけ解決する
			SkinnedAnimationRuntimeData* runtime =
				TryGetSkinnedAnimationRuntime(world, entity);
			if (!runtime) {
				return;
			}
			// メッシュが無効な場合はアニメーションデータをクリアして終了
			if (!renderer.mesh) {
				runtime->palette.clear();
				runtime->availableClips.clear();
				runtime->initialized = false;
				runtime->currentDuration = 0.0f;
				return;
			}

			context.skinnedAnimationManager->RequestLoadAsync(*context.assetDatabase, renderer.mesh);

			// メッシュに対応するアニメーションセットを取得
			const SkinnedMeshAnimationSet* animationSet = context.skinnedAnimationManager->Find(renderer.mesh);
			if (!animationSet || !animationSet->valid || animationSet->clips.empty()) {
				runtime->palette.clear();
				runtime->availableClips.clear();
				runtime->currentDuration = 0.0f;
				return;
			}

			// メッシュが切り替わったか
			bool meshChanged = (!runtime->initialized || runtime->mesh != renderer.mesh);
			if (meshChanged) {

				// 初期クリップの名前を取得
				runtime->currentClip = ResolveInitialClip(*animationSet, anim.clip);

				runtime->mesh = renderer.mesh;
				runtime->initialized = true;
				runtime->bindSkeleton = animationSet->skeleton;
				runtime->skeleton = runtime->bindSkeleton;
				runtime->palette.resize(animationSet->skeleton.joints.size());
				runtime->fromClip.clear();
				runtime->toClip.clear();
				runtime->time = 0.0f;
				runtime->fromTime = 0.0f;
				runtime->blendTime = 0.0f;
				runtime->inTransition = false;
				runtime->animationFinished = false;
				runtime->repeatCount = 0;

				// クリップ一覧を作り直す
				runtime->availableClips.clear();
				runtime->availableClips.reserve(animationSet->clipOrder.size());
				for (const std::string& clipName : animationSet->clipOrder) {

					runtime->availableClips.emplace_back(clipName);
				}
			}

			// 再生するクリップの名前を取得
			std::string desiredClip = ResolveInitialClip(*animationSet, anim.clip);
			bool clipChanged = false;
			if (!desiredClip.empty() && desiredClip != runtime->currentClip && !runtime->inTransition) {

				clipChanged = true;
				// 再生開始の瞬間に終了フラグを下ろす、遷移中も前回のtrueを残さない
				runtime->animationFinished = false;
				runtime->fromClip = runtime->currentClip;
				runtime->toClip = desiredClip;
				runtime->fromTime = runtime->time;
				runtime->blendTime = 0.0f;
				runtime->inTransition = (anim.transitionDuration > 0.0f);
				if (!runtime->inTransition) {

					runtime->currentClip = desiredClip;
					runtime->time = 0.0f;
				}
			}

			// アニメーションの更新を行うか
			bool allowTimeAdvance = anim.enabled && (context.mode == WorldMode::Play || anim.playInEditMode);
			// Play中はTimeScale適用済みのdeltaTime、EditのプレビューはdeltaTimeが0になるためTimeScale非適用のリアル時間を使う
			float sourceDelta = (context.mode == WorldMode::Play) ? context.deltaTime : context.unscaledDeltaTime;
			// フレーム時間を再生速度に応じてスケーリング
			float deltaTime = allowTimeAdvance ? sourceDelta * anim.playbackSpeed : 0.0f;

			// 停止中、遷移一時停止中、非ループ再生完了後はポーズが変わらないため再計算を省く
			const bool transitionAdvances =
				runtime->inTransition && deltaTime != 0.0f;
			const bool clipAdvances =
				!runtime->inTransition && deltaTime != 0.0f &&
				(anim.loop || !runtime->animationFinished);
			const bool poseDirty = meshChanged || clipChanged ||
				transitionAdvances || clipAdvances ||
				!runtime->initialized || runtime->palette.empty();
			if (!poseDirty) {
				return;
			}

			// スケルトンをバインドポーズで初期化
			runtime->skeleton = runtime->bindSkeleton;
			// アニメーション遷移していないとき
			if (!runtime->inTransition) {

				const AnimationData& clip = animationSet->clips.at(runtime->currentClip);
				runtime->currentDuration = clip.duration;
				if (0.0f < clip.duration) {

					if (anim.loop) {
						float nextTime = runtime->time + deltaTime;
						// ループ再生している場合、再生時間がアニメーションクリップの長さを超えたらループ回数を増やす
						if (clip.duration <= nextTime) {

							++runtime->repeatCount;
						}
						runtime->time = std::fmod(nextTime, clip.duration);
						runtime->animationFinished = false;
					} else {

						// ループ再生していない場合、再生時間がアニメーションクリップの長さを超えないようにする
						runtime->time = (std::min)(runtime->time + deltaTime, clip.duration);
						runtime->animationFinished = clip.duration <= runtime->time;
					}
				}

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
				runtime->blendTime += deltaTime;
				// 遷移時間に対する経過時間の割合を計算
				float alpha = 0.0f < anim.transitionDuration ? runtime->blendTime / anim.transitionDuration : 1.0f;

				// 遷移元と遷移先のアニメーションクリップの対応するノードのアニメーションをブレンドしてスケルトンに適用
				auto fromTrackIt = animationSet->clipJointTracks.find(runtime->fromClip);
				auto toTrackIt = animationSet->clipJointTracks.find(runtime->toClip);
				if (fromTrackIt != animationSet->clipJointTracks.end() && toTrackIt != animationSet->clipJointTracks.end()) {

					BlendClipsToSkeleton(runtime->skeleton, fromTrackIt->second, runtime->fromTime, toTrackIt->second, 0.0f, alpha);
				}

				// 遷移が完了したら遷移フラグを下ろして遷移先のアニメーションクリップを再生状態にする
				if (1.0f <= alpha) {

					runtime->inTransition = false;
					runtime->currentClip = runtime->toClip;
					runtime->time = 0.0f;
				}
			}
			// スケルトンの階層を更新してGPU用のパレットを構築
			UpdateSkeletonHierarchy(runtime->skeleton);
			BuildPalette(runtime->skeleton, animationSet->skinCluster, runtime->palette);
			++runtime->poseGeneration;
		});
}

std::string Engine::SkinnedAnimationSystem::ResolveInitialClip(
	const SkinnedMeshAnimationSet& animationSet, const std::string& requestedClip) {

	if (!requestedClip.empty()) {
		if (animationSet.clips.contains(requestedClip)) {
			return requestedClip;
		}
	}
	if (animationSet.clips.contains("Default")) {
		return "Default";
	}
	if (!animationSet.clipOrder.empty()) {
		return animationSet.clipOrder.front();
	}
	if (!animationSet.clips.empty()) {
		return animationSet.clips.begin()->first;
	}
	return {};
}
