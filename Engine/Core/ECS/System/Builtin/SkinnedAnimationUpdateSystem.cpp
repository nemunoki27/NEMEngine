#include "SkinnedAnimationUpdateSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>

//============================================================================
//	SkinnedAnimationUpdateSystem classMethods
//============================================================================

namespace {

	// 並列処理するジョイントの閾値
	static constexpr size_t kParallelJointThreshold = 8;
}

template<>
Engine::Vector3 Engine::SkinnedAnimationUpdateSystem::SampleKeyframes<Engine::Vector3>(
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
Engine::Quaternion Engine::SkinnedAnimationUpdateSystem::SampleKeyframes<Engine::Quaternion>(
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

void Engine::SkinnedAnimationUpdateSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// MeshRendererComponentとSkinnedAnimationComponentを持つエンティティに対して処理を行う
	world.ForEach<MeshRendererComponent, SkinnedAnimationComponent>([&]([[maybe_unused]] const Entity& entity,
		MeshRendererComponent& renderer, SkinnedAnimationComponent& anim) {

			// メッシュが無効な場合はアニメーションデータをクリアして終了
			if (!renderer.mesh) {
				anim.palette.clear();
				anim.runtimeAvailableClips.clear();
				anim.runtimeInitialized = false;
				return;
			}

			context.skinnedAnimationManager->RequestLoadAsync(*context.assetDatabase, renderer.mesh);

			// メッシュに対応するアニメーションセットを取得
			const SkinnedMeshAnimationSet* animationSet = context.skinnedAnimationManager->Find(renderer.mesh);
			if (!animationSet || !animationSet->valid || animationSet->clips.empty()) {
				anim.palette.clear();
				anim.runtimeAvailableClips.clear();
				return;
			}

			// メッシュが切り替わったか
			bool meshChanged = (!anim.runtimeInitialized || anim.runtimeMesh != renderer.mesh);
			if (meshChanged) {

				// 初期クリップの名前を取得
				anim.runtimeCurrentClip = ResolveInitialClip(*animationSet, anim.clip);

				anim.runtimeMesh = renderer.mesh;
				anim.runtimeInitialized = true;
				anim.runtimeBindSkeleton = animationSet->skeleton;
				anim.runtimeSkeleton = anim.runtimeBindSkeleton;
				anim.palette.resize(animationSet->skeleton.joints.size());
				anim.runtimeFromClip.clear();
				anim.runtimeToClip.clear();
				anim.runtimeTime = 0.0f;
				anim.runtimeFromTime = 0.0f;
				anim.runtimeNextTime = 0.0f;
				anim.runtimeBlendTime = 0.0f;
				anim.runtimeInTransition = false;
				anim.runtimeAnimationFinished = false;
				anim.runtimeRepeatCount = 0;

				// クリップ一覧を作り直す
				anim.runtimeAvailableClips.clear();
				anim.runtimeAvailableClips.reserve(animationSet->clipOrder.size());
				for (const std::string& clipName : animationSet->clipOrder) {

					anim.runtimeAvailableClips.emplace_back(clipName);
				}
			}

			// 再生するクリップの名前を取得
			std::string desiredClip = ResolveInitialClip(*animationSet, anim.clip);
			if (!desiredClip.empty() && desiredClip != anim.runtimeCurrentClip && !anim.runtimeInTransition) {

				anim.runtimeFromClip = anim.runtimeCurrentClip;
				anim.runtimeToClip = desiredClip;
				anim.runtimeFromTime = anim.runtimeTime;
				anim.runtimeNextTime = 0.0f;
				anim.runtimeBlendTime = 0.0f;
				anim.runtimeInTransition = (anim.transitionDuration > 0.0f);
				if (!anim.runtimeInTransition) {

					anim.runtimeCurrentClip = desiredClip;
					anim.runtimeTime = 0.0f;
				}
			}

			// アニメーションの更新を行うか
			bool allowTimeAdvance = anim.enabled && (context.mode == WorldMode::Play || anim.playInEditMode);
			// フレーム時間を再生速度に応じてスケーリング
			float deltaTime = allowTimeAdvance ? context.deltaTime * anim.playbackSpeed : 0.0f;
			// スケルトンをバインドポーズで初期化
			anim.runtimeSkeleton = anim.runtimeBindSkeleton;
			// アニメーション遷移していないとき
			if (!anim.runtimeInTransition) {

				const AnimationData& clip = animationSet->clips.at(anim.runtimeCurrentClip);
				if (0.0f < clip.duration) {

					if (anim.loop) {
						float nextTime = anim.runtimeTime + deltaTime;
						// ループ再生している場合、再生時間がアニメーションクリップの長さを超えたらループ回数を増やす
						if (clip.duration <= nextTime) {

							++anim.runtimeRepeatCount;
						}
						anim.runtimeTime = std::fmod(nextTime, clip.duration);
						anim.runtimeAnimationFinished = false;
					} else {

						// ループ再生していない場合、再生時間がアニメーションクリップの長さを超えないようにする
						anim.runtimeTime = (std::min)(anim.runtimeTime + deltaTime, clip.duration);
						anim.runtimeAnimationFinished = clip.duration <= anim.runtimeTime;
					}
				}

				// 現在再生中のアニメーションクリップをスケルトンに適用
				auto trackIt = animationSet->clipJointTracks.find(anim.runtimeCurrentClip);
				if (trackIt != animationSet->clipJointTracks.end()) {

					ApplyClipToSkeleton(anim.runtimeSkeleton, trackIt->second, anim.runtimeTime);
				}
			}
			// アニメーション遷移中
			else {

				// 遷移元と遷移先のアニメーションクリップを取得
				const AnimationData& fromClip = animationSet->clips.at(anim.runtimeFromClip);
				const AnimationData& toClip = animationSet->clips.at(anim.runtimeToClip);
				if (0.0f < fromClip.duration) {
					anim.runtimeFromTime = std::fmod(anim.runtimeFromTime + deltaTime, fromClip.duration);
				}
				if (0.0f < toClip.duration) {
					anim.runtimeNextTime = std::fmod(anim.runtimeNextTime + deltaTime, toClip.duration);
				}

				// 遷移時間を進める
				anim.runtimeBlendTime += deltaTime;
				// 遷移時間に対する経過時間の割合を計算
				float alpha = 0.0f < anim.transitionDuration ? anim.runtimeBlendTime / anim.transitionDuration : 1.0f;

				// 遷移元と遷移先のアニメーションクリップの対応するノードのアニメーションをブレンドしてスケルトンに適用
				auto fromTrackIt = animationSet->clipJointTracks.find(anim.runtimeFromClip);
				auto toTrackIt = animationSet->clipJointTracks.find(anim.runtimeToClip);
				if (fromTrackIt != animationSet->clipJointTracks.end() &&
					toTrackIt != animationSet->clipJointTracks.end()) {

					BlendClipsToSkeleton(anim.runtimeSkeleton, fromTrackIt->second, anim.runtimeFromTime,
						toTrackIt->second, anim.runtimeNextTime, alpha);
				}

				// 遷移が完了したら遷移フラグを下ろして遷移先のアニメーションクリップを再生状態にする
				if (1.0f <= alpha) {

					anim.runtimeInTransition = false;
					anim.runtimeCurrentClip = anim.runtimeToClip;
					anim.runtimeTime = anim.runtimeNextTime;
				}
			}
			// スケルトンの階層を更新してGPU用のパレットを構築
			UpdateSkeletonHierarchy(anim.runtimeSkeleton);
			BuildPalette(anim.runtimeSkeleton, animationSet->skinCluster, anim.palette);
		});
}

void Engine::SkinnedAnimationUpdateSystem::ApplyClipToSkeleton(Skeleton& skeleton,
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
	// ジョイントの数が閾値未満の場合は単純にループで処理し、閾値以上の場合は並列処理する
	if (skeleton.joints.size() < kParallelJointThreshold) {
		for (Joint& joint : skeleton.joints) {

			applyOne(joint);
		}
	} else {

		std::for_each(std::execution::par_unseq, skeleton.joints.begin(), skeleton.joints.end(), applyOne);
	}
}

void Engine::SkinnedAnimationUpdateSystem::BlendClipsToSkeleton(Skeleton& skeleton,
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
	// ジョイントの数が閾値未満の場合は単純にループで処理し、閾値以上の場合は並列処理する
	if (skeleton.joints.size() < kParallelJointThreshold) {
		for (Joint& joint : skeleton.joints) {

			blendOne(joint);
		}
	} else {

		std::for_each(std::execution::par_unseq, skeleton.joints.begin(), skeleton.joints.end(), blendOne);
	}
}

void Engine::SkinnedAnimationUpdateSystem::UpdateSkeletonHierarchy(Skeleton& skeleton) {

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

void Engine::SkinnedAnimationUpdateSystem::BuildPalette(const Skeleton& skeleton,
	const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette) {

	outPalette.resize(skeleton.joints.size());

	// 各ジョイントに対して、GPU用のパレットを構築するラムダ関数
	auto buildOne = [&skeleton, &skinCluster, &outPalette](WellForGPU& well) {

		size_t jointIndex = static_cast<size_t>(&well - outPalette.data());

		well.skeletonSpaceMatrix = skinCluster.inverseBindPoseMatrices[jointIndex] *
			skeleton.joints[jointIndex].skeletonSpaceMatrix;
		well.skeletonSpaceInverseTransposeMatrix = Matrix4x4::Transpose(Matrix4x4::Inverse(well.skeletonSpaceMatrix));
		};
	// ジョイントの数が閾値未満の場合は単純にループで処理し、閾値以上の場合は並列処理する
	if (outPalette.size() < kParallelJointThreshold) {
		for (WellForGPU& well : outPalette) {

			buildOne(well);
		}
	} else {

		std::for_each(std::execution::par_unseq, outPalette.begin(), outPalette.end(), buildOne);
	}
}

std::string Engine::SkinnedAnimationUpdateSystem::ResolveInitialClip(
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