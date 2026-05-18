#include "AnimationClipEvaluator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	AnimationClipEvaluator classMethods
//============================================================================

namespace {

	bool LerpValue(const Engine::AnimationPropertyValue& from,
		const Engine::AnimationPropertyValue& to, float t, Engine::AnimationPropertyValue& out);
	float BridgeInterp(float t, Engine::CurveInterpolationMode mode);

	bool SameBinding(const Engine::AnimationPropertyBinding& lhs, const Engine::AnimationPropertyBinding& rhs) {

		return lhs.componentName == rhs.componentName && lhs.propertyPath == rhs.propertyPath;
	}

	const Engine::AnimationPropertyValue* FindBaseValue(
		const Engine::AnimationCurveTrack& track,
		std::span<const Engine::AnimationPreviewBaseValue> baseValues) {

		// Add/MultiplyはPreview開始時の値を基準にするため、Trackと同じbindingを探す。
		for (const Engine::AnimationPreviewBaseValue& baseValue : baseValues) {
			if (SameBinding(baseValue.binding, track.binding)) {
				return &baseValue.value;
			}
		}
		return nullptr;
	}

	bool ReadChannels(const Engine::AnimationCurveTrack& track, float time, float* values, uint32_t valueCount) {

		// 通常評価では全チャンネルをCurveChannelから読む。
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

		Engine::CurveQuaternionAxisKey axisKey{};
		axisKey.axes = { Engine::Axis::X };
		axisKey.customAxis = Engine::Vector3(1.0f, 0.0f, 0.0f);
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

	Engine::Vector3 GetQuaternionAxisDirection(const Engine::CurveQuaternionAxisKey& axisKey) {

		Engine::Vector3 axis = axisKey.useCustomAxis ? axisKey.customAxis : Engine::GetDirection(axisKey.axes);
		if (axis.Length() <= 0.001f) {
			axis = Engine::Vector3(1.0f, 0.0f, 0.0f);
		}
		return axis.Normalize();
	}

	bool EvaluateQuaternionAxisAngleTrack(const Engine::AnimationCurveTrack& track, float time,
		Engine::AnimationPropertyValue& outValue) {

		if (!IsQuaternionAxisAngleTrack(track)) {
			return false;
		}

		const uint32_t axisKeyIndex = FindQuaternionAxisIndex(track, time);
		const Engine::CurveQuaternionAxisKey axisKey = GetQuaternionAxisKey(track, axisKeyIndex);
		const Engine::Vector3 axis = GetQuaternionAxisDirection(axisKey);
		const float angleDegrees = track.channels[1].Evaluate(time);
		outValue = Engine::Quaternion::Normalize(
			Engine::Quaternion::MakeAxisAngle(axis, Math::DegToRad(angleDegrees)));
		return true;
	}

	bool HasAnyKey(const Engine::AnimationCurveTrack& track) {

		// Track追加直後は全チャンネルが空なので、適用対象から外す。
		for (const Engine::CurveChannel& channel : track.channels) {
			if (!channel.keys.empty()) {
				return true;
			}
		}
		return false;
	}

	bool ReadValueChannels(const Engine::AnimationPropertyValue& value, float* values, uint32_t valueCount) {

		// 現在値やPreview開始時の値をfloat配列へ展開する。
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

		// キーが無いチャンネルはfallback値を残し、キーがあるチャンネルだけ上書きする。
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

		// Override用の評価。部分キー編集で未編集成分を壊さないために使う。
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

		// LoopBridge外は通常のClip内時刻を評価する。
		if (!time.inLoopBridge) {
			return EvaluateTrackWithFallback(track, time.clipTime, fallback, outValue);
		}

		// Bridge区間はduration時点から0秒時点へ補間する。
		Engine::AnimationPropertyValue endValue{};
		Engine::AnimationPropertyValue beginValue{};
		if (!EvaluateTrackWithFallback(track, clip.duration, fallback, endValue) ||
			!EvaluateTrackWithFallback(track, 0.0f, fallback, beginValue)) {
			return false;
		}
		return LerpValue(endValue, beginValue,
			BridgeInterp(time.bridgeT, clip.loopBridge.interpolation), outValue);
	}

	bool CombineValue(const Engine::AnimationPropertyValue& baseValue,
		const Engine::AnimationPropertyValue& curveValue,
		Engine::AnimationApplyMode mode,
		Engine::QuaternionMultiplyOrder quaternionOrder,
		Engine::AnimationPropertyValue& out) {

		if (mode == Engine::AnimationApplyMode::Override) {
			out = curveValue;
			return true;
		}

		// Add/MultiplyはPreview開始時に記録した基準値だけを使う。
		// 毎フレーム現在値へ積み上げると、ScrubやPlayで値が破綻する。
		if (const float* base = std::get_if<float>(&baseValue)) {
			if (const float* curve = std::get_if<float>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Vector2* base = std::get_if<Engine::Vector2>(&baseValue)) {
			if (const Engine::Vector2* curve = std::get_if<Engine::Vector2>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Vector3* base = std::get_if<Engine::Vector3>(&baseValue)) {
			if (const Engine::Vector3* curve = std::get_if<Engine::Vector3>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Vector4* base = std::get_if<Engine::Vector4>(&baseValue)) {
			if (const Engine::Vector4* curve = std::get_if<Engine::Vector4>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Color3* base = std::get_if<Engine::Color3>(&baseValue)) {
			if (const Engine::Color3* curve = std::get_if<Engine::Color3>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Color4* base = std::get_if<Engine::Color4>(&baseValue)) {
			if (const Engine::Color4* curve = std::get_if<Engine::Color4>(&curveValue)) {
				out = mode == Engine::AnimationApplyMode::Add ? *base + *curve : *base * *curve;
				return true;
			}
		}
		if (const Engine::Quaternion* base = std::get_if<Engine::Quaternion>(&baseValue)) {
			if (const Engine::Quaternion* curve = std::get_if<Engine::Quaternion>(&curveValue)) {
				if (mode != Engine::AnimationApplyMode::Multiply) {
					return false;
				}
				out = quaternionOrder == Engine::QuaternionMultiplyOrder::BaseThenCurve ?
					Engine::Quaternion::Normalize(*base * *curve) :
					Engine::Quaternion::Normalize(*curve * *base);
				return true;
			}
		}

		return false;
	}

	float BridgeInterp(float t, Engine::CurveInterpolationMode mode) {

		// LoopBridgeはハンドルを持たないので、BezierはLinear相当で扱う。
		t = (std::clamp)(t, 0.0f, 1.0f);
		switch (mode) {
		case Engine::CurveInterpolationMode::Constant:
			return 0.0f;
		case Engine::CurveInterpolationMode::Spline:
		case Engine::CurveInterpolationMode::Squad:
			return t * t * (3.0f - 2.0f * t);
		case Engine::CurveInterpolationMode::Linear:
		case Engine::CurveInterpolationMode::Bezier:
		default:
			return t;
		}
	}

	bool LerpValue(const Engine::AnimationPropertyValue& from,
		const Engine::AnimationPropertyValue& to, float t, Engine::AnimationPropertyValue& out) {

		// LoopBridgeで使う型別補間。
		if (const float* a = std::get_if<float>(&from)) {
			if (const float* b = std::get_if<float>(&to)) {
				out = *a + (*b - *a) * t;
				return true;
			}
		}
		if (const Engine::Vector2* a = std::get_if<Engine::Vector2>(&from)) {
			if (const Engine::Vector2* b = std::get_if<Engine::Vector2>(&to)) {
				out = Engine::Vector2::Lerp(*a, *b, t);
				return true;
			}
		}
		if (const Engine::Vector3* a = std::get_if<Engine::Vector3>(&from)) {
			if (const Engine::Vector3* b = std::get_if<Engine::Vector3>(&to)) {
				out = Engine::Vector3::Lerp(*a, *b, t);
				return true;
			}
		}
		if (const Engine::Vector4* a = std::get_if<Engine::Vector4>(&from)) {
			if (const Engine::Vector4* b = std::get_if<Engine::Vector4>(&to)) {
				out = Engine::Vector4(
					a->x + (b->x - a->x) * t,
					a->y + (b->y - a->y) * t,
					a->z + (b->z - a->z) * t,
					a->w + (b->w - a->w) * t);
				return true;
			}
		}
		if (const Engine::Color3* a = std::get_if<Engine::Color3>(&from)) {
			if (const Engine::Color3* b = std::get_if<Engine::Color3>(&to)) {
				out = Engine::Color3::Lerp(*a, *b, t);
				return true;
			}
		}
		if (const Engine::Color4* a = std::get_if<Engine::Color4>(&from)) {
			if (const Engine::Color4* b = std::get_if<Engine::Color4>(&to)) {
				out = Engine::Color4::Lerp(*a, *b, t);
				return true;
			}
		}
		if (const Engine::Quaternion* a = std::get_if<Engine::Quaternion>(&from)) {
			if (const Engine::Quaternion* b = std::get_if<Engine::Quaternion>(&to)) {
				out = Engine::Quaternion::Lerp(*a, *b, t);
				return true;
			}
		}
		return false;
	}
}

bool Engine::AnimationClipEvaluator::EvaluateTrack(const AnimationCurveTrack& track,
	float time, AnimationPropertyValue& outValue) {

	// Trackの保存型に応じて、floatチャンネル配列を実際のPropertyValueへ戻す。
	float v[4]{};
	switch (track.binding.valueType) {
	case AnimationValueType::Float:
		if (!ReadChannels(track, time, v, 1)) {
			return false;
		}
		outValue = v[0];
		return true;
	case AnimationValueType::Vector2:
		if (!ReadChannels(track, time, v, 2)) {
			return false;
		}
		outValue = Vector2(v[0], v[1]);
		return true;
	case AnimationValueType::Vector3:
		if (!ReadChannels(track, time, v, 3)) {
			return false;
		}
		outValue = Vector3(v[0], v[1], v[2]);
		return true;
	case AnimationValueType::Vector4:
		if (!ReadChannels(track, time, v, 4)) {
			return false;
		}
		outValue = Vector4(v[0], v[1], v[2], v[3]);
		return true;
	case AnimationValueType::Color3:
		if (!ReadChannels(track, time, v, 3)) {
			return false;
		}
		outValue = Color3(v[0], v[1], v[2]);
		return true;
	case AnimationValueType::Color4:
		if (!ReadChannels(track, time, v, 4)) {
			return false;
		}
		outValue = Color4(v[0], v[1], v[2], v[3]);
		return true;
	case AnimationValueType::Quaternion:
		if (IsQuaternionAxisAngleTrack(track)) {
			return EvaluateQuaternionAxisAngleTrack(track, time, outValue);
		}
		if (!ReadChannels(track, time, v, 4)) {
			return false;
		}
		outValue = Quaternion::Normalize(Quaternion(v[0], v[1], v[2], v[3]));
		return true;
	default:
		break;
	}
	return false;
}

bool Engine::AnimationClipEvaluator::EvaluateTrack(const AnimationCurveTrack& track,
	const AnimationResolvedTime& time, const AnimationClipAsset& clip, AnimationPropertyValue& outValue) {

	// 通常再生中はClip時刻をそのまま評価する。
	if (!time.inLoopBridge) {
		return EvaluateTrack(track, time.clipTime, outValue);
	}

	// Bridge中は終端値と先頭値を取り、設定された補間でつなぐ。
	AnimationPropertyValue endValue{};
	AnimationPropertyValue beginValue{};
	if (!EvaluateTrack(track, clip.duration, endValue) || !EvaluateTrack(track, 0.0f, beginValue)) {
		return false;
	}
	return LerpValue(endValue, beginValue,
		BridgeInterp(time.bridgeT, clip.loopBridge.interpolation), outValue);
}

Engine::AnimationResolvedTime Engine::AnimationClipEvaluator::ResolveClipEvaluationTime(
	const AnimationClipAsset& clip, float playbackTime) {

	AnimationResolvedTime result{};
	const float duration = (std::max)(clip.duration, 0.001f);

	// 非Loopは範囲外をClampするだけ。
	if (!clip.loop) {
		result.clipTime = (std::clamp)(playbackTime, 0.0f, duration);
		return result;
	}

	if (clip.loopBridge.enabled && 0.0f < clip.loopBridge.duration) {
		// Bridge有効時はduration + bridgeDurationを1周期として扱う。
		const float bridgeDuration = (std::max)(clip.loopBridge.duration, 0.001f);
		const float period = duration + bridgeDuration;
		float localTime = std::fmod((std::max)(0.0f, playbackTime), period);
		if (localTime <= duration) {
			result.clipTime = localTime;
			return result;
		}
		result.clipTime = duration;
		result.inLoopBridge = true;
		result.bridgeT = (localTime - duration) / bridgeDuration;
		return result;
	}

	result.clipTime = std::fmod((std::max)(0.0f, playbackTime), duration);
	return result;
}

float Engine::AnimationClipEvaluator::GetPlaybackDuration(const AnimationClipAsset& clip) {

	const float duration = (std::max)(clip.duration, 0.001f);
	return clip.loop && clip.loopBridge.enabled ? duration + (std::max)(clip.loopBridge.duration, 0.001f) : duration;
}

bool Engine::AnimationClipEvaluator::ApplyTrack(ECSWorld& world, const Entity& entity,
	const AnimationCurveTrack& track, const AnimationClipAsset& clip,
	const AnimationResolvedTime& time, const AnimationPropertyValue* baseValueOrNull) {

	// 登録済みPropertyだけを適用する。Missing Propertyは編集を止めずにスキップする。
	const AnimationPropertyDescriptor* desc = AnimationPropertyRegistry::GetInstance().Find(
		track.binding.componentName, track.binding.propertyPath);
	if (!desc || !desc->hasComponent || !desc->setValue || !desc->hasComponent(world, entity)) {
		return false;
	}
	if (!HasAnyKey(track)) {
		return false;
	}

	AnimationPropertyValue curveValue{};
	AnimationPropertyValue currentValue{};
	const AnimationPropertyValue* fallbackValue = baseValueOrNull;
	if (!fallbackValue && desc->getValue && desc->getValue(world, entity, currentValue)) {
		// 通常適用時は現在値をfallbackにして、未編集成分を残す。
		fallbackValue = &currentValue;
	}

	// Overrideではキーが無いチャンネルを現在値のまま残す。
	// Property追加直後やXだけキーを打った状態で、Y/ZやScaleがdefault値へ戻るのを防ぐ。
	if (track.applyMode == AnimationApplyMode::Override && fallbackValue) {
		if (!EvaluateTrackWithFallback(track, time, clip, *fallbackValue, curveValue)) {
			return false;
		}
	} else if (!EvaluateTrack(track, time, clip, curveValue)) {
		return false;
	}

	AnimationPropertyValue finalValue{};
	if (track.applyMode == AnimationApplyMode::Override) {
		finalValue = curveValue;
	} else {
		if (!baseValueOrNull || !CombineValue(*baseValueOrNull, curveValue,
			track.applyMode, track.quaternionMultiplyOrder, finalValue)) {
			return false;
		}
	}
	return desc->setValue(world, entity, finalValue);
}

void Engine::AnimationClipEvaluator::ApplyClip(ECSWorld& world, const Entity& entity,
	const AnimationClipAsset& clip, float time, std::span<const AnimationPreviewBaseValue> baseValues) {

	if (!world.IsAlive(entity)) {
		return;
	}

	// Clip全体で一度だけ再生時刻を解決し、各Trackに同じ時刻を渡す。
	const AnimationResolvedTime resolvedTime = ResolveClipEvaluationTime(clip, time);
	for (const AnimationCurveTrack& track : clip.curveTracks) {

		const AnimationPropertyValue* baseValue = FindBaseValue(track, baseValues);
		ApplyTrack(world, entity, track, clip, resolvedTime, baseValue);
	}
}
