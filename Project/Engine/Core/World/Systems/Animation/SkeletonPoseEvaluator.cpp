#include "SkeletonPoseEvaluator.h"

using namespace Engine;

// c++
#include <algorithm>

namespace {

	Engine::Vector3 SampleKeyframes( const std::vector<KeyframeVector3>& keys, float time) {

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

	Engine::Quaternion SampleKeyframes( const std::vector<KeyframeQuaternion>& keys, float time) {

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

}

namespace Engine::SkeletonPoseEvaluator {

	void ApplyClipToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& jointTracks, float time) {

		if (skeleton.joints.empty()) {
			return;
		}
		if (jointTracks.size() != skeleton.joints.size()) {
			return;
		}

		Joint* joints = skeleton.joints.data();
		const NodeAnimation* const* tracks = jointTracks.data();
		// 各ジョイントに対してアニメーションを適用するラムダ関数
		auto applyOne = [joints, tracks, time](Joint& joint) {

			const size_t jointIndex = static_cast<size_t>(&joint - joints);
			const NodeAnimation* track = tracks[jointIndex];
			if (!track) {
				return;
			}

			// ジョイントに対して、対応するノードのアニメーションを適用
			if (!track->translate.keyframes.empty()) {
				joint.transform.translation = SampleKeyframes(track->translate.keyframes, time);
			}
			if (!track->rotate.keyframes.empty()) {
				joint.transform.rotation = SampleKeyframes(track->rotate.keyframes, time);
			}
			if (!track->scale.keyframes.empty()) {
				joint.transform.scale = SampleKeyframes(track->scale.keyframes, time);
			}
			};
		// ジョイント単位の並列化はスケジューリングのオーバーヘッドが処理本体を上回るため直列で回す
		for (Joint& joint : skeleton.joints) {

			applyOne(joint);
		}
	}

	void BlendClipsToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& fromTracks, float fromTime,
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
		auto blendOne = [joints, fromTrackPtr, toTrackPtr, fromTime, toTime, alpha](Joint& joint) {

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
					fromT = SampleKeyframes(fromTrack->translate.keyframes, fromTime);
				}
				if (!fromTrack->rotate.keyframes.empty()) {
					fromR = SampleKeyframes(fromTrack->rotate.keyframes, fromTime);
				}
				if (!fromTrack->scale.keyframes.empty()) {
					fromS = SampleKeyframes(fromTrack->scale.keyframes, fromTime);
				}
			}
			// 遷移先
			if (toTrack) {
				if (!toTrack->translate.keyframes.empty()) {
					toT = SampleKeyframes(toTrack->translate.keyframes, toTime);
				}
				if (!toTrack->rotate.keyframes.empty()) {
					toR = SampleKeyframes(toTrack->rotate.keyframes, toTime);
				}
				if (!toTrack->scale.keyframes.empty()) {
					toS = SampleKeyframes(toTrack->scale.keyframes, toTime);
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

	void UpdateSkeletonHierarchy(Skeleton& skeleton) {

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

	void BuildPalette(const Skeleton& skeleton, const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette) {

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
}
