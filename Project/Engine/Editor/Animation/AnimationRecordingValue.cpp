#include "AnimationKeyRecording.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Evaluation/AnimationTrackSampling.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>

using namespace Engine;

namespace {

	// 適用前の数値を基準値から逆算する
	bool InvertApplication(const AnimationCurveTrack* track, const AnimationPropertyValue& baseline,
		const AnimationPropertyValue& desired, double time, AnimationPropertyValue& result) {

		if (!track || track->applyMode == AnimationApplyMode::Override) { result = desired; return true; }
		if (const auto* rotation = std::get_if<Quaternion>(&desired)) {
			const auto* base = std::get_if<Quaternion>(&baseline);
			if (!base || track->applyMode != AnimationApplyMode::Multiply) return false;
			result = Quaternion::Normalize(track->quaternionMultiplyOrder == QuaternionMultiplyOrder::BaseThenCurve ?
				Quaternion::Inverse(*base) * *rotation : *rotation * Quaternion::Inverse(*base));
			return true;
		}
		const auto count = GetAnimationValueTypeChannelCount(track->binding.valueType);
		std::array<float, 4> base{}, values{};
		if (!AnimationTrackSampling::ReadValueChannels(baseline, base.data(), count) ||
			!AnimationTrackSampling::ReadValueChannels(desired, values.data(), count)) return false;
		for (uint32_t i = 0; i < count; ++i) {
			if (track->applyMode == AnimationApplyMode::Add) values[i] -= base[i];
			else if (std::abs(base[i]) > 0.000001f) values[i] /= base[i];
			else if (std::abs(values[i]) > 0.000001f) return false;
			else values[i] = i < track->channels.size() ? track->channels[i].Evaluate(static_cast<float>(time)) : 1.0f;
			if (!std::isfinite(values[i])) return false;
		}
		return AnimationTrackSampling::WriteValueChannels(track->binding.valueType,
			std::span<const float>(values.data(), count), result);
	}
}

bool Engine::ResolveRecordedAnimationValue(const AnimationClipAsset& clip, const AnimationCurveTrack* track,
	const RecordedAnimationProperty& change, const AnimationPropertyValue& desired, double time, AnimationPropertyValue& value) {

	const auto& baseline = change.baseline ? *change.baseline : change.before;
	AnimationPropertyValue transformed = desired;
	if (clip.relativeTransform && change.binding.componentName == "Transform") {
		AnimationPropertyValue neutral = baseline;
		if (track) {
			AnimationPropertyValue raw;
			if (AnimationClipEvaluator::EvaluateTrack(*track, 0.0f, raw)) {
				if (!AnimationValueOperations::CombineValue(baseline, raw, track->applyMode,
					track->quaternionMultiplyOrder, neutral)) return false;
			}
		}
		const auto& path = change.binding.propertyPath;
		if (path == "localPos") {
			const auto* position = std::get_if<Vector3>(&desired);
			const auto* origin = std::get_if<Vector3>(&baseline);
			const auto* start = std::get_if<Vector3>(&neutral);
			if (!position || !origin || !start) return false;
			transformed = *start + Vector3::TransferNormal(*position - *origin,
				Quaternion::MakeRotateMatrix(Quaternion::Inverse(change.originRotation)));
		} else if (path == "localRotation") {
			const auto* rotation = std::get_if<Quaternion>(&desired);
			const auto* origin = std::get_if<Quaternion>(&baseline);
			const auto* start = std::get_if<Quaternion>(&neutral);
			if (!rotation || !origin || !start) return false;
			transformed = Quaternion::Normalize(*start * Quaternion::Inverse(*origin) * *rotation);
		} else if (path == "localPos2D") {
			const auto* position = std::get_if<Vector2>(&desired);
			const auto* origin = std::get_if<Vector2>(&baseline);
			const auto* start = std::get_if<Vector2>(&neutral);
			if (!position || !origin || !start) return false;
			const float angle = Math::DegToRad(change.originAngle);
			const auto delta = *position - *origin;
			transformed = *start + Vector2(delta.x * std::cos(angle) + delta.y * std::sin(angle),
				-delta.x * std::sin(angle) + delta.y * std::cos(angle));
		} else if (path == "localRotationZ") {
			const auto* angle = std::get_if<float>(&desired);
			const auto* origin = std::get_if<float>(&baseline);
			const auto* start = std::get_if<float>(&neutral);
			if (!angle || !origin || !start) return false;
			transformed = *start + (*angle - *origin);
		}
	}
	return InvertApplication(track, baseline, transformed, time, value);
}
