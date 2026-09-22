#include "AnimationTrackSampling.h"
#include "AnimationValueOperations.h"
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationTrackSampling {

	using namespace AnimationValueOperations;

	bool ReadChannels(const Engine::AnimationCurveTrack& track, float time, float* values, uint32_t valueCount) {

		// 通常評価では全チャンネルをCurveChannelから読む
		if (track.channels.size() < valueCount) {
			return false;
		}
		for (uint32_t i = 0; i < valueCount; ++i) {
			values[i] = track.channels[i].Evaluate(time);
		}
		return true;
	}

	bool IsQuaternionAxisAngleTrack(const Engine::AnimationCurveTrack& track) {

		return track.binding.valueType == Engine::AnimationValueType::Quaternion &&
			track.channels.size() == 2 &&
			track.channels[0].name == "Axis" &&
			track.channels[1].name == "Angle";
	}

	Engine::CurveQuaternionAxisKey GetQuaternionAxisKey(const Engine::AnimationCurveTrack& track, uint32_t keyIndex) {

		if (keyIndex < track.quaternionAxisKeys.size()) {
			return track.quaternionAxisKeys[keyIndex];
		}

		Engine::CurveQuaternionAxisKey axisKey = Engine::QuaternionAxisKeyUtility::MakeDefault();
		if (keyIndex < track.channels[0].keys.size()) {
			const int32_t axisIndex = (std::clamp)(
				static_cast<int32_t>(std::round(track.channels[0].keys[keyIndex].value)), 0, 2);
			axisKey.axes = { static_cast<Engine::Axis>(axisIndex) };
		}
		return axisKey;
	}

	uint32_t FindQuaternionAxisIndex(const Engine::AnimationCurveTrack& track, float time) {

		if (track.channels[0].keys.empty()) {
			return 0;
		}
		if (time <= track.channels[0].keys.front().time) {
			return 0;
		}
		if (track.channels[0].keys.back().time <= time) {
			return static_cast<uint32_t>(track.channels[0].keys.size() - 1);
		}

		auto nextIt = std::upper_bound(track.channels[0].keys.begin(), track.channels[0].keys.end(), time,
			[](float t, const Engine::CurveKey& key) {
				return t < key.time;
			});
		return static_cast<uint32_t>((nextIt - 1) - track.channels[0].keys.begin());
	}

	bool EvaluateQuaternionAxisAngleTrack(const Engine::AnimationCurveTrack& track, float time,
		Engine::AnimationPropertyValue& outValue) {

		if (!IsQuaternionAxisAngleTrack(track)) {
			return false;
		}

		const uint32_t axisKeyIndex = FindQuaternionAxisIndex(track, time);
		const Engine::CurveQuaternionAxisKey axisKey = GetQuaternionAxisKey(track, axisKeyIndex);
		const Engine::Vector3 axis = Engine::QuaternionAxisKeyUtility::GetAxisDirection(axisKey);
		const float angleDegrees = track.channels[1].Evaluate(time);
		outValue = Engine::Quaternion::Normalize(
			Engine::Quaternion::MakeAxisAngle(axis, Math::DegToRad(angleDegrees)));
		return true;
	}

	bool HasAnyKey(const Engine::AnimationCurveTrack& track) {

		// Track追加直後は全チャンネルが空なので、適用対象から外す
		for (const Engine::CurveChannel& channel : track.channels) {
			if (!channel.keys.empty()) {
				return true;
			}
		}
		return false;
	}

	bool ReadValueChannels(const Engine::AnimationPropertyValue& value, float* values, uint32_t valueCount) {

		// 現在値やPreview開始時の値をfloat配列へ展開する
		if (const float* v = std::get_if<float>(&value)) {
			if (valueCount != 1) {
				return false;
			}
			values[0] = *v;
			return true;
		}
		if (const Engine::Vector2* v = std::get_if<Engine::Vector2>(&value)) {
			if (valueCount != 2) {
				return false;
			}
			values[0] = v->x;
			values[1] = v->y;
			return true;
		}
		if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value)) {
			if (valueCount != 3) {
				return false;
			}
			values[0] = v->x;
			values[1] = v->y;
			values[2] = v->z;
			return true;
		}
		if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value)) {
			if (valueCount != 4) {
				return false;
			}
			values[0] = v->x;
			values[1] = v->y;
			values[2] = v->z;
			values[3] = v->w;
			return true;
		}
		if (const Engine::Color3* v = std::get_if<Engine::Color3>(&value)) {
			if (valueCount != 3) {
				return false;
			}
			values[0] = v->r;
			values[1] = v->g;
			values[2] = v->b;
			return true;
		}
		if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value)) {
			if (valueCount != 4) {
				return false;
			}
			values[0] = v->r;
			values[1] = v->g;
			values[2] = v->b;
			values[3] = v->a;
			return true;
		}
		if (const Engine::Quaternion* v = std::get_if<Engine::Quaternion>(&value)) {
			if (valueCount != 4) {
				return false;
			}
			values[0] = v->x;
			values[1] = v->y;
			values[2] = v->z;
			values[3] = v->w;
			return true;
		}
		return false;
	}

	bool ReadChannelsWithFallback(const Engine::AnimationCurveTrack& track, float time,
		const Engine::AnimationPropertyValue& fallback, float* values, uint32_t valueCount) {

		// キーが無いチャンネルはfallback値を残し、キーがあるチャンネルだけ上書きする
		if (track.channels.size() < valueCount || !HasAnyKey(track) ||
			!ReadValueChannels(fallback, values, valueCount)) {
			return false;
		}
		for (uint32_t i = 0; i < valueCount; ++i) {
			if (!track.channels[i].keys.empty()) {
				values[i] = track.channels[i].Evaluate(time);
			}
		}
		return true;
	}

	bool EvaluateTrackWithFallback(const Engine::AnimationCurveTrack& track, float time,
		const Engine::AnimationPropertyValue& fallback, Engine::AnimationPropertyValue& outValue) {

		// Override用の評価で部分キー編集で未編集成分を壊さないために使う
		float v[4]{};
		switch (track.binding.valueType) {
		case Engine::AnimationValueType::Float:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 1)) {
				return false;
			}
			outValue = v[0];
			return true;
		case Engine::AnimationValueType::Vector2:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 2)) {
				return false;
			}
			outValue = Engine::Vector2(v[0], v[1]);
			return true;
		case Engine::AnimationValueType::Vector3:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 3)) {
				return false;
			}
			outValue = Engine::Vector3(v[0], v[1], v[2]);
			return true;
		case Engine::AnimationValueType::Vector4:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 4)) {
				return false;
			}
			outValue = Engine::Vector4(v[0], v[1], v[2], v[3]);
			return true;
		case Engine::AnimationValueType::Color3:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 3)) {
				return false;
			}
			outValue = Engine::Color3(v[0], v[1], v[2]);
			return true;
		case Engine::AnimationValueType::Color4:
			if (!ReadChannelsWithFallback(track, time, fallback, v, 4)) {
				return false;
			}
			outValue = Engine::Color4(v[0], v[1], v[2], v[3]);
			return true;
		case Engine::AnimationValueType::Quaternion:
			if (IsQuaternionAxisAngleTrack(track)) {
				return EvaluateQuaternionAxisAngleTrack(track, time, outValue);
			}
			if (!ReadChannelsWithFallback(track, time, fallback, v, 4)) {
				return false;
			}
			outValue = Engine::Quaternion::Normalize(Engine::Quaternion(v[0], v[1], v[2], v[3]));
			return true;
		default:
			break;
		}
		return false;
	}

	bool EvaluateTrackWithFallback(const Engine::AnimationCurveTrack& track,
		const Engine::AnimationResolvedTime& time, const Engine::AnimationClipAsset& clip,
		const Engine::AnimationPropertyValue& fallback, Engine::AnimationPropertyValue& outValue) {

		// LoopBridge外は通常のClip内時刻を評価する
		if (!time.inLoopBridge) {
			return EvaluateTrackWithFallback(track, time.clipTime, fallback, outValue);
		}

		// Bridge区間はduration時点から0秒時点へ補間する
		Engine::AnimationPropertyValue endValue{};
		Engine::AnimationPropertyValue beginValue{};
		if (!EvaluateTrackWithFallback(track, clip.duration, fallback, endValue) ||
			!EvaluateTrackWithFallback(track, 0.0f, fallback, beginValue)) {
			return false;
		}
		return LerpValue(endValue, beginValue,
			Engine::AnimationClipEvaluator::BridgeInterp(time.bridgeT, clip.loopBridge.interpolation), outValue);
	}
}
