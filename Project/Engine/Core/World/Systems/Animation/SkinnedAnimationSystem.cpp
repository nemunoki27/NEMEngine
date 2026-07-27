#include "SkinnedAnimationSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	SkinnedAnimationSystem classMethods
//============================================================================

template<>
Engine::Vector3 Engine::SkinnedAnimationSystem::SampleKeyframes<Engine::Vector3>(
	const std::vector<KeyframeVector3>& keys, float time) {

	if (keys.empty()) {
		return Engine::Vector3::AnyInit(0.0f);
	}
	if (keys.size() == 1 || time <= keys.front().time) {
		return keys.front().value;
	}
	if (keys.back().time <= time) {
		return keys.back().value;
	}

	auto upper = std::lower_bound(keys.begin() + 1, keys.end(), time,
		[](const Engine::KeyframeVector3& key, float value) {
			return key.time < value;
		});

	const Engine::KeyframeVector3& next = *upper;
	const Engine::KeyframeVector3& prev = *(upper - 1);

	float span = next.time - prev.time;
	if (span <= 0.0f) {
		return next.value;
	}

	float t = (time - prev.time) / span;
	return Engine::Vector3::Lerp(prev.value, next.value, t);
}

template<>
Engine::Quaternion Engine::SkinnedAnimationSystem::SampleKeyframes<Engine::Quaternion>(
	const std::vector<KeyframeQuaternion>& keys, float time) {

	if (keys.empty()) {
		return Engine::Quaternion::Identity();
	}
	if (keys.size() == 1 || time <= keys.front().time) {
		return keys.front().value;
	}
	if (keys.back().time <= time) {
		return keys.back().value;
	}

	auto upper = std::lower_bound(keys.begin() + 1, keys.end(), time,
		[](const Engine::KeyframeQuaternion& key, float value) {
			return key.time < value;
		});

	const Engine::KeyframeQuaternion& next = *upper;
	const Engine::KeyframeQuaternion& prev = *(upper - 1);

	float span = next.time - prev.time;
	if (span <= 0.0f) {
		return next.value;
	}

	float t = (time - prev.time) / span;
	return Engine::Quaternion::Lerp(prev.value, next.value, t).Normalize();
}

void Engine::SkinnedAnimationSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	world.ForEach<MeshRendererComponent, SkinnedAnimationComponent, SkinnedAnimationRuntimeComponent>(
		[&](const Entity& entity, MeshRendererComponent& renderer,
			SkinnedAnimationComponent& anim, [[maybe_unused]] SkinnedAnimationRuntimeComponent& runtimeComponent) {

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

			// 一時停止中や再生停止中はdeltaTimeが0でポーズが前フレームと同一になるため再計算を省く
			const bool poseDirty = meshChanged || clipChanged || runtime->inTransition ||
				deltaTime != 0.0f || !runtime->initialized || runtime->palette.empty();
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
		});
}

void Engine::SkinnedAnimationSystem::ApplyClipToSkeleton(Skeleton& skeleton,
	const std::vector<const NodeAnimation*>& jointTracks, float time) {

	if (skeleton.joints.empty()) {
		return;
	}
	if (jointTracks.size() != skeleton.joints.size()) {
		return;
	}

	Joint* joints = skeleton.joints.data();
	const NodeAnimation* const* tracks = jointTracks.data();
	// 各ジョイントに対してアニメーションを適用するラムダ関数
	auto applyOne = [this, joints, tracks, time](Joint& joint) {

		const size_t jointIndex = static_cast<size_t>(&joint - joints);
		const NodeAnimation* track = tracks[jointIndex];
		if (!track) {
			return;
		}

		// ジョイントに対して、対応するノードのアニメーションを適用
		if (!track->translate.keyframes.empty()) {
			joint.transform.translation = SampleKeyframes<Vector3>(track->translate.keyframes, time);
		}
		if (!track->rotate.keyframes.empty()) {
			joint.transform.rotation = SampleKeyframes<Quaternion>(track->rotate.keyframes, time);
		}
		if (!track->scale.keyframes.empty()) {
			joint.transform.scale = SampleKeyframes<Vector3>(track->scale.keyframes, time);
		}
		};
	// ジョイント単位の並列化はスケジューリングのオーバーヘッドが処理本体を上回るため直列で回す
	for (Joint& joint : skeleton.joints) {

		applyOne(joint);
	}
}

void Engine::SkinnedAnimationSystem::BlendClipsToSkeleton(Skeleton& skeleton,
	const std::vector<const NodeAnimation*>& fromTracks, float fromTime,
	const std::vector<const NodeAnimation*>& toTracks, float toTime, float alpha) {

	alpha = (std::clamp)(alpha, 0.0f, 1.0f);

	if (skeleton.joints.empty()) {
		return;
	}
	if (fromTracks.size() != skeleton.joints.size() ||
		toTracks.size() != skeleton.joints.size()) {
		return;
	}

	Joint* joints = skeleton.joints.data();
	const NodeAnimation* const* fromTrackPtr = fromTracks.data();
	const NodeAnimation* const* toTrackPtr = toTracks.data();
	// 各ジョイントに対して、遷移元と遷移先のアニメーションをブレンドして適用するラムダ関数
	auto blendOne = [this, joints, fromTrackPtr, toTrackPtr, fromTime, toTime, alpha](Joint& joint) {

		const size_t jointIndex = static_cast<size_t>(&joint - joints);

		// ジョイントに対して、対応するノードの遷移元と遷移先のアニメーションをブレンドして適用
		const NodeAnimation* fromTrack = fromTrackPtr[jointIndex];
		const NodeAnimation* toTrack = toTrackPtr[jointIndex];

		Vector3 fromT = joint.transform.translation;
		Vector3 toT = joint.transform.translation;
		Quaternion fromR = joint.transform.rotation;
		Quaternion toR = joint.transform.rotation;
		Vector3 fromS = joint.transform.scale;
		Vector3 toS = joint.transform.scale;
		// 遷移元
		if (fromTrack) {
			if (!fromTrack->translate.keyframes.empty()) {
				fromT = SampleKeyframes<Vector3>(fromTrack->translate.keyframes, fromTime);
			}
			if (!fromTrack->rotate.keyframes.empty()) {
				fromR = SampleKeyframes<Quaternion>(fromTrack->rotate.keyframes, fromTime);
			}
			if (!fromTrack->scale.keyframes.empty()) {
				fromS = SampleKeyframes<Vector3>(fromTrack->scale.keyframes, fromTime);
			}
		}
		// 遷移先
		if (toTrack) {
			if (!toTrack->translate.keyframes.empty()) {
				toT = SampleKeyframes<Vector3>(toTrack->translate.keyframes, toTime);
			}
			if (!toTrack->rotate.keyframes.empty()) {
				toR = SampleKeyframes<Quaternion>(toTrack->rotate.keyframes, toTime);
			}
			if (!toTrack->scale.keyframes.empty()) {
				toS = SampleKeyframes<Vector3>(toTrack->scale.keyframes, toTime);
			}
		}
		// ブレンドして適用
		joint.transform.translation = Vector3::Lerp(fromT, toT, alpha);
		joint.transform.rotation = Quaternion::Lerp(fromR, toR, alpha).Normalize();
		joint.transform.scale = Vector3::Lerp(fromS, toS, alpha);
		};
	// ジョイント単位の並列化はスケジューリングのオーバーヘッドが処理本体を上回るため直列で回す
	for (Joint& joint : skeleton.joints) {

		blendOne(joint);
	}
}

void Engine::SkinnedAnimationSystem::UpdateSkeletonHierarchy(Skeleton& skeleton) {

	for (auto& joint : skeleton.joints) {

		// ジョイントのローカル変換行列を作成
		joint.localMatrix = Matrix4x4::MakeAffineMatrix(joint.transform.scale,
			joint.transform.rotation, joint.transform.translation);

		if (joint.parent.has_value()) {

			joint.skeletonSpaceMatrix = joint.localMatrix * skeleton.joints[*joint.parent].skeletonSpaceMatrix;
		} else {

			joint.skeletonSpaceMatrix = joint.localMatrix;
		}
	}
}

void Engine::SkinnedAnimationSystem::BuildPalette(const Skeleton& skeleton,
	const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette) {

	outPalette.resize(skeleton.joints.size());

	// 各ジョイントに対して、GPU用のパレットを構築するラムダ関数
	auto buildOne = [&skeleton, &skinCluster, &outPalette](WellForGPU& well) {

		size_t jointIndex = static_cast<size_t>(&well - outPalette.data());

		well.skeletonSpaceMatrix = skinCluster.inverseBindPoseMatrices[jointIndex] *
			skeleton.joints[jointIndex].skeletonSpaceMatrix;
		well.skeletonSpaceInverseTransposeMatrix = Matrix4x4::Transpose(Matrix4x4::Inverse(well.skeletonSpaceMatrix));
		};
	// ジョイント単位の並列化はスケジューリングのオーバーヘッドが処理本体を上回るため直列で回す
	for (WellForGPU& well : outPalette) {

		buildOne(well);
	}
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
