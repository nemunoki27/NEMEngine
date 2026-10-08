#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationClipEditorUtility {

	void CollectKeyTimes(std::span<const CurveChannel> channels, std::vector<float>& outTimes) {

		// Channelごとのキー時刻をまとめる
		outTimes.clear();
		for (const CurveChannel& channel : channels) {
			for (const CurveKey& key : channel.keys) {
				outTimes.emplace_back(key.time);
			}
		}

		std::sort(outTimes.begin(), outTimes.end());
		outTimes.erase(
			std::unique(outTimes.begin(), outTimes.end(), [](float a, float b) { return std::abs(a - b) <= 0.0005f; }),
			outTimes.end());
	}

	CurveInterpolationMode FindKeyInterpolationAt(const CurveChannel& channel, float time) {

		// 同じ時刻の補間方法を引き継ぐ
		for (const CurveKey& key : channel.keys) {
			if (std::abs(key.time - time) <= 0.0005f) {
				return key.interpolation;
			}
		}
		return CurveInterpolationMode::Linear;
	}

	CurveQuaternionAxisKey MakeAxisKeyFromQuaternion(const Quaternion& rotation, float& outAngleDegrees) {

		// Quaternionを回転軸と角度へ分解する
		const Quaternion normalized = Quaternion::Normalize(rotation);
		const float w = (std::clamp)(normalized.w, -1.0f, 1.0f);
		const float angleRadians = 2.0f * std::acos(w);
		const float sinHalf = std::sqrt((std::max)(0.0f, 1.0f - w * w));

		Vector3 axis(1.0f, 0.0f, 0.0f);
		if (0.0001f < sinHalf) {
			axis = Vector3(normalized.x / sinHalf, normalized.y / sinHalf, normalized.z / sinHalf);
		}

		CurveQuaternionAxisKey axisKey{};
		axisKey.useCustomAxis = true;
		axisKey.customAxis = axis;
		outAngleDegrees = Math::RadToDeg(angleRadians);
		return QuaternionAxisKeyUtility::Sanitize(axisKey);
	}

	CurveQuaternion BuildQuaternionEditorCurve(const AnimationCurveTrack& track) {

		CurveQuaternion curve{};
		curve.channels[0].keys.clear();
		curve.channels[1].keys.clear();
		curve.axisKeys.clear();

		if (track.channels.size() == 2 && track.channels[0].name == "Axis" && track.channels[1].name == "Angle") {
			curve.channels[0] = track.channels[0];
			curve.channels[1] = track.channels[1];
			curve.axisKeys = track.quaternionAxisKeys;
			curve.EnsureAxisKeyCount();
			return curve;
		}

		std::vector<float> times{};
		CollectKeyTimes(track.channels, times);

		// キー時刻ごとの評価値を回転軸と角度へ変換する
		for (float time : times) {
			AnimationPropertyValue value{};
			Quaternion rotation = Quaternion::Identity();
			if (AnimationClipEvaluator::EvaluateTrack(track, time, value)) {
				if (const Quaternion* evaluated = std::get_if<Quaternion>(&value)) {
					rotation = *evaluated;
				}
			}

			float angleDegrees = 0.0f;
			CurveQuaternionAxisKey axisKey = MakeAxisKeyFromQuaternion(rotation, angleDegrees);
			const CurveInterpolationMode interpolation =
				track.channels.empty() ? CurveInterpolationMode::Linear : FindKeyInterpolationAt(track.channels.front(), time);

			curve.channels[0].AddKey(time, 0.0f, interpolation);
			curve.channels[1].AddKey(time, angleDegrees, interpolation);
			curve.axisKeys.emplace_back(axisKey);
		}
		curve.EnsureAxisKeyCount();
		return curve;
	}

	void StoreQuaternionEditorCurve(const CurveQuaternion& curve, AnimationCurveTrack& track) {

		// 回転軸と角度のChannelを分けて保持する
		track.channels = {
			curve.channels[0],
			curve.channels[1],
		};
		track.quaternionAxisKeys = curve.axisKeys;
	}

	bool IsQuaternionAxisAngleTrack(const AnimationCurveTrack& track) {

		return track.binding.valueType == AnimationValueType::Quaternion && track.channels.size() == 2 &&
			   track.channels[0].name == "Axis" && track.channels[1].name == "Angle";
	}

	CurveQuaternionAxisKey& GetQuaternionAxisKeyForEdit(AnimationCurveTrack& track, uint32_t keyIndex) {

		while (track.quaternionAxisKeys.size() <= keyIndex) {
			track.quaternionAxisKeys.emplace_back(QuaternionAxisKeyUtility::MakeDefault());
		}
		return track.quaternionAxisKeys[keyIndex];
	}

	float GetPrimaryAxisValue(const CurveQuaternionAxisKey& axisKey) {

		if (axisKey.axes.empty()) {
			return static_cast<float>(EnumAdapter<Axis>::GetIndex(Axis::X));
		}
		return static_cast<float>(EnumAdapter<Axis>::GetIndex(axisKey.axes.front()));
	}

	void SortQuaternionAxisKeys(AnimationCurveTrack& track) {

		if (!IsQuaternionAxisAngleTrack(track)) {
			return;
		}

		struct AxisKeyPair {

			CurveKey key;
			CurveQuaternionAxisKey axis;
		};

		// 回転軸とキーを同じ組で並べ替える
		std::vector<AxisKeyPair> pairs{};
		pairs.reserve(track.channels[0].keys.size());
		for (uint32_t i = 0; i < track.channels[0].keys.size(); ++i) {
			CurveQuaternionAxisKey axisKey =
				i < track.quaternionAxisKeys.size() ? track.quaternionAxisKeys[i] : QuaternionAxisKeyUtility::MakeDefault();
			pairs.push_back({track.channels[0].keys[i], std::move(axisKey)});
		}
		std::sort(pairs.begin(), pairs.end(),
			[](const AxisKeyPair& lhs, const AxisKeyPair& rhs) { return lhs.key.time < rhs.key.time; });

		track.quaternionAxisKeys.resize(pairs.size());
		for (uint32_t i = 0; i < pairs.size(); ++i) {
			track.channels[0].keys[i] = pairs[i].key;
			track.quaternionAxisKeys[i] = std::move(pairs[i].axis);
			track.channels[0].keys[i].value = GetPrimaryAxisValue(track.quaternionAxisKeys[i]);
			track.channels[0].keys[i].interpolation = CurveInterpolationMode::Constant;
		}
	}
}
