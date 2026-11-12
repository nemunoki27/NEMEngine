#include "AnimationKeyRecording.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Animation/Evaluation/AnimationTrackSampling.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <type_traits>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

namespace {

	// 同じ時刻のキーを更新して補間設定を保持する
	uint32_t SetKey(CurveChannel& channel, float time, float value) {

		uint32_t index = 0;
		if (FindKeyIndexAtTime(channel, time, index)) {
			channel.keys[index].value = value;
			return index;
		}
		return channel.AddKey(time, value, CurveInterpolationMode::Linear);
	}

	// 値型の成分を対応するチャンネルへ設定する
	void SetValueKey(AnimationCurveTrack& track, float time, const AnimationPropertyValue& value) {

		if (const auto* rotation = std::get_if<Quaternion>(&value)) {
			if (IsQuaternionEulerTrack(track)) {
				Vector3 degrees = Quaternion::ToEulerDegrees(*rotation);
				if (track.channels.size() == 3) {
					Vector3 previous(track.channels[0].Evaluate(time), track.channels[1].Evaluate(time), track.channels[2].Evaluate(time));
					degrees = Vector3::MakeContinuousDegrees(degrees, previous);
					SetKey(track.channels[0], time, degrees.x);
					SetKey(track.channels[1], time, degrees.y);
					SetKey(track.channels[2], time, degrees.z);
				}
				return;
			}
			CurveQuaternion curve = BuildQuaternionEditorCurve(track);
			float angle = 0.0f;
			const auto axis = MakeAxisKeyFromQuaternion(*rotation, angle);
			uint32_t existing = 0;
			const bool replace = FindKeyIndexAtTime(curve.channels[0], time, existing);
			const auto index = SetKey(curve.channels[0], time, 0.0f);
			SetKey(curve.channels[1], time, angle);
			if (!replace) curve.axisKeys.insert(curve.axisKeys.begin() + index, axis);
			else if (index < curve.axisKeys.size()) curve.axisKeys[index] = axis;
			curve.EnsureAxisKeyCount();
			StoreQuaternionEditorCurve(curve, track);
			return;
		}
		std::visit([&](const auto& typed) {
			using T = std::decay_t<decltype(typed)>;
			std::array<float, 4> values{};
			size_t count = 0;
			if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int32_t>) {
				values[0] = static_cast<float>(typed); count = 1;
			} else if constexpr (std::is_same_v<T, Vector2>) {
				values = {typed.x, typed.y}; count = 2;
			} else if constexpr (std::is_same_v<T, Vector3>) {
				values = {typed.x, typed.y, typed.z}; count = 3;
			} else if constexpr (std::is_same_v<T, Vector4>) {
				values = {typed.x, typed.y, typed.z, typed.w}; count = 4;
			} else if constexpr (std::is_same_v<T, Color3>) {
				values = {typed.r, typed.g, typed.b}; count = 3;
			} else if constexpr (std::is_same_v<T, Color4>) {
				values = {typed.r, typed.g, typed.b, typed.a}; count = 4;
			}
			for (size_t i = 0; i < count && i < track.channels.size(); ++i) SetKey(track.channels[i], time, values[i]);
		}, value);
	}
}

bool Engine::RecordAnimationProperties(AnimationClipAsset& clip, std::span<const RecordedAnimationProperty> changes,
	double time, double frameRate) {

	if (!std::isfinite(time) || time < 0.0 || time > FLT_MAX || !std::isfinite(frameRate) || frameRate <= 0.0) return false;
	const auto resolved = AnimationClipEvaluator::ResolveClipEvaluationTime(clip, static_cast<float>(time));
	if (clip.loop && resolved.inLoopBridge) return false;
	const double keyTime = clip.loop ? resolved.clipTime : time;
	std::vector<RecordedAnimationProperty> prepared;
	for (const auto& change : changes) {
		if (change.before.index() != static_cast<size_t>(change.binding.valueType) ||
			change.after.index() != change.before.index()) return false;
		if (ApproxEqualValue(change.before, change.after)) continue;
		const auto found = std::ranges::find_if(clip.curveTracks, [&](const auto& track) {
			return AnimationValueOperations::SameBinding(track.binding, change.binding);
		});
		auto key = change;
		const auto* track = found == clip.curveTracks.end() ? nullptr : &*found;
		if (!ResolveRecordedAnimationValue(clip, track, change, change.before, keyTime, key.before) ||
			!ResolveRecordedAnimationValue(clip, track, change, change.after, keyTime, key.after)) return false;
		std::array<float, 4> values{};
		const auto count = GetAnimationValueTypeChannelCount(change.binding.valueType);
		if (!AnimationTrackSampling::ReadValueChannels(key.before, values.data(), count) ||
			!std::all_of(values.begin(), values.begin() + count, [](float value) { return std::isfinite(value); })) return false;
		if (!AnimationTrackSampling::ReadValueChannels(key.after, values.data(), count) ||
			!std::all_of(values.begin(), values.begin() + count, [](float value) { return std::isfinite(value); })) return false;
		prepared.push_back(std::move(key));
	}
	for (const auto& key : prepared) {
		auto found = std::ranges::find_if(clip.curveTracks, [&](const auto& track) {
			return AnimationValueOperations::SameBinding(track.binding, key.binding);
		});
		if (found == clip.curveTracks.end()) {
			AnimationCurveTrack track;
			track.binding = key.binding;
			SetupTrackInitialValue(track, key.before);
			NormalizeAnimationTrackChannels(track);
			clip.curveTracks.push_back(std::move(track));
			found = std::prev(clip.curveTracks.end());
			if (keyTime > 0.0005) SetValueKey(*found, 0.0f, key.before);
		}
		SetValueKey(*found, static_cast<float>(keyTime), key.after);
	}
	if (!prepared.empty()) clip.duration = (std::max)(clip.duration, static_cast<float>(keyTime));
	return !prepared.empty();
}

void Engine::SetAnimationPropertyKey(AnimationCurveTrack& track, float time, const AnimationPropertyValue& value) {

	SetValueKey(track, time, value);
}
