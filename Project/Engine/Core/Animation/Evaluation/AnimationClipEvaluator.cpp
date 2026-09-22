#include "AnimationClipEvaluator.h"

//============================================================================
//	include
//============================================================================
#include "AnimationValueOperations.h"
#include "AnimationTrackSampling.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace Engine::AnimationValueOperations;
using namespace Engine::AnimationTrackSampling;

//============================================================================
//	AnimationClipEvaluator classMethods
//============================================================================
namespace {

	std::optional<Engine::AnimationPropertyDescriptor> ResolveProperty(
		Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::AnimationPropertyBinding& binding) {

		std::optional<Engine::AnimationPropertyDescriptor> descriptor =
			Engine::AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, binding.componentName, binding.propertyPath, binding.valueType);
		if (descriptor) {
			return descriptor;
		}

		// 同じ未解決Bindingを毎フレーム出力しない
		const std::string key = binding.componentName + "." + binding.propertyPath + ":" +
			Engine::ToString(binding.valueType);
		static std::unordered_set<std::string> warnedBindings;
		if (warnedBindings.emplace(key).second) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"AnimationClipEvaluator: Animation Propertyが見つかりません Component={} Property={} type={}",
				binding.componentName, binding.propertyPath, Engine::ToString(binding.valueType));
		}
		return std::nullopt;
	}

	bool ComputeTrackValue(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::AnimationPropertyDescriptor& desc, const Engine::AnimationCurveTrack& track,
		const Engine::AnimationClipAsset& clip, const Engine::AnimationResolvedTime& time,
		const Engine::AnimationPropertyValue* baseValueOrNull, Engine::AnimationPropertyValue& out) {

		// 適用と値評価で同じ計算を使うため、書き込み手前までの最終値を計算する
		if (!HasAnyKey(track)) {
			return false;
		}

		Engine::AnimationPropertyValue curveValue{};
		Engine::AnimationPropertyValue currentValue{};
		const Engine::AnimationPropertyValue* fallbackValue = baseValueOrNull;
		if (!fallbackValue && desc.getValue && desc.getValue(world, entity, currentValue)) {
			// 通常適用時は現在値をfallbackにして、未編集成分を残す
			fallbackValue = &currentValue;
		}

		// Overrideではキーが無いチャンネルをfallback値のまま残す
		if (track.applyMode == Engine::AnimationApplyMode::Override && fallbackValue) {
			if (!EvaluateTrackWithFallback(track, time, clip, *fallbackValue, curveValue)) {
				return false;
			}
		} else if (!Engine::AnimationClipEvaluator::EvaluateTrack(track, time, clip, curveValue)) {
			return false;
		}

		if (track.applyMode == Engine::AnimationApplyMode::Override) {
			out = curveValue;
			return true;
		}
		return baseValueOrNull && CombineValue(*baseValueOrNull, curveValue,
			track.applyMode, track.quaternionMultiplyOrder, out);
	}
}

bool Engine::AnimationClipEvaluator::EvaluateTrack(const AnimationCurveTrack& track,
	float time, AnimationPropertyValue& outValue) {

	// Trackの保存型に応じて、floatチャンネル配列を実際のPropertyValueへ戻す
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

	// 通常再生中はClip時刻をそのまま評価する
	if (!time.inLoopBridge) {
		return EvaluateTrack(track, time.clipTime, outValue);
	}

	// Bridge中は終端値と先頭値を取り、設定された補間でつなぐ
	AnimationPropertyValue endValue{};
	AnimationPropertyValue beginValue{};
	if (!EvaluateTrack(track, clip.duration, endValue) || !EvaluateTrack(track, 0.0f, beginValue)) {
		return false;
	}
	return LerpValue(endValue, beginValue,
		Engine::AnimationClipEvaluator::BridgeInterp(time.bridgeT, clip.loopBridge.interpolation), outValue);
}

bool Engine::AnimationClipEvaluator::ApplyTrack(ECSWorld& world, const Entity& entity,
	const AnimationCurveTrack& track, const AnimationClipAsset& clip,
	const AnimationResolvedTime& time, const AnimationPropertyValue* baseValueOrNull) {

	// 静的/動的どちらのPropertyも解決する、Missing Propertyは編集を止めずにスキップする
	const std::optional<AnimationPropertyDescriptor> descOpt = ResolveProperty(world, entity, track.binding);
	if (!descOpt) {
		return false;
	}
	const AnimationPropertyDescriptor& desc = *descOpt;
	if (!desc.hasComponent || !desc.setValue || !desc.hasComponent(world, entity)) {
		return false;
	}

	AnimationPropertyValue finalValue{};
	if (!ComputeTrackValue(world, entity, desc, track, clip, time, baseValueOrNull, finalValue)) {
		return false;
	}
	return desc.setValue(world, entity, finalValue);
}

void Engine::AnimationClipEvaluator::ApplyClip(ECSWorld& world, const Entity& entity,
	const AnimationClipAsset& clip, float time, std::span<const AnimationPreviewBaseValue> baseValues) {

	if (!world.IsAlive(entity)) {
		return;
	}

	// Clip全体で一度だけ再生時刻を解決し、各Trackに同じ時刻を渡す
	const AnimationResolvedTime resolvedTime = ResolveClipEvaluationTime(clip, time);

	// 向き相対クリップは値評価してから基準姿勢で合成して書き込む
	if (clip.relativeTransform) {

		std::vector<AnimationEvaluatedValue> values;
		EvaluateClipValues(world, entity, clip, resolvedTime, baseValues, values);
		// クリップ開始姿勢(t=0)を中立として相対化するため、t=0の値も評価して渡す
		AnimationResolvedTime neutralTime{};
		std::vector<AnimationEvaluatedValue> neutralValues;
		EvaluateClipValues(world, entity, clip, neutralTime, baseValues, neutralValues);
		ComposeRelativeTransform(values, baseValues, neutralValues);
		WriteValues(world, entity, values);
		return;
	}

	for (const AnimationCurveTrack& track : clip.curveTracks) {

		const AnimationPropertyValue* baseValue = FindBaseValue(track, baseValues);
		ApplyTrack(world, entity, track, clip, resolvedTime, baseValue);
	}
}

void Engine::AnimationClipEvaluator::EvaluateClipValues(ECSWorld& world, const Entity& entity,
	const AnimationClipAsset& clip, const AnimationResolvedTime& time,
	std::span<const AnimationPreviewBaseValue> baseValues, std::vector<AnimationEvaluatedValue>& outValues) {

	outValues.clear();
	if (!world.IsAlive(entity)) {
		return;
	}

	// 書き込まずに各Trackの最終値だけを集める、クロスフェードの合成元に使う
	for (const AnimationCurveTrack& track : clip.curveTracks) {

		const std::optional<AnimationPropertyDescriptor> descOpt = ResolveProperty(world, entity, track.binding);
		if (!descOpt || !descOpt->hasComponent || !descOpt->hasComponent(world, entity)) {
			continue;
		}
		const AnimationPropertyValue* baseValue = FindBaseValue(track, baseValues);
		AnimationEvaluatedValue evaluated{};
		evaluated.binding = track.binding;
		if (!ComputeTrackValue(world, entity, *descOpt, track, clip, time, baseValue, evaluated.value)) {
			continue;
		}
		outValues.emplace_back(std::move(evaluated));
	}
}

void Engine::AnimationClipEvaluator::WriteValues(ECSWorld& world, const Entity& entity,
	std::span<const AnimationEvaluatedValue> values) {

	if (!world.IsAlive(entity)) {
		return;
	}

	for (const AnimationEvaluatedValue& value : values) {

		const std::optional<AnimationPropertyDescriptor> descOpt = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, value.binding.componentName, value.binding.propertyPath, value.binding.valueType);
		if (!descOpt || !descOpt->setValue || !descOpt->hasComponent || !descOpt->hasComponent(world, entity)) {
			continue;
		}
		descOpt->setValue(world, entity, value.value);
	}
}

void Engine::AnimationClipEvaluator::PreserveUnkeyedChannels(ECSWorld& world, const Entity& entity,
	const AnimationClipAsset& clip, std::vector<AnimationEvaluatedValue>& values) {

	if (!world.IsAlive(entity)) {
		return;
	}

	// bindingに対応するtrackを引く
	const auto findTrack = [&](const AnimationPropertyBinding& binding) -> const AnimationCurveTrack* {
		for (const AnimationCurveTrack& track : clip.curveTracks) {
			if (SameBinding(track.binding, binding)) {
				return &track;
			}
		}
		return nullptr;
		};
	// 成分indexに対応するチャネルがキーを持つか
	const auto keyed = [](const AnimationCurveTrack& track, size_t channelIndex) {
		return channelIndex < track.channels.size() && !track.channels[channelIndex].keys.empty();
		};

	for (AnimationEvaluatedValue& value : values) {

		const AnimationCurveTrack* track = findTrack(value.binding);
		if (!track) {
			continue;
		}

		// 全チャネルがキーを持つなら現在値を読む必要はない
		bool anyUnkeyed = false;
		const size_t channelCount = GetAnimationValueTypeChannelCount(value.binding.valueType);
		for (size_t i = 0; i < channelCount; ++i) {
			if (!keyed(*track, i)) { anyUnkeyed = true; break; }
		}
		if (!anyUnkeyed) {
			continue;
		}

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, value.binding.componentName, value.binding.propertyPath, value.binding.valueType);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}
		AnimationPropertyValue current{};
		if (!desc->getValue(world, entity, current) || current.index() != value.value.index()) {
			continue;
		}

		// キーのある成分はアニメ値、キーの無い成分は現在値を採用する
		const auto merge = [&](auto animComponent, auto currentComponent, size_t channelIndex) {
			return keyed(*track, channelIndex) ? animComponent : currentComponent;
			};
		if (Vector2* v2 = std::get_if<Vector2>(&value.value)) {
			const Vector2& c = std::get<Vector2>(current);
			value.value = Vector2(merge(v2->x, c.x, 0), merge(v2->y, c.y, 1));
		} else if (Vector3* v3 = std::get_if<Vector3>(&value.value)) {
			const Vector3& c = std::get<Vector3>(current);
			value.value = Vector3(merge(v3->x, c.x, 0), merge(v3->y, c.y, 1), merge(v3->z, c.z, 2));
		} else if (Vector4* v4 = std::get_if<Vector4>(&value.value)) {
			const Vector4& c = std::get<Vector4>(current);
			value.value = Vector4(merge(v4->x, c.x, 0), merge(v4->y, c.y, 1), merge(v4->z, c.z, 2), merge(v4->w, c.w, 3));
		} else if (Color3* col3 = std::get_if<Color3>(&value.value)) {
			const Color3& c = std::get<Color3>(current);
			value.value = Color3(merge(col3->r, c.r, 0), merge(col3->g, c.g, 1), merge(col3->b, c.b, 2));
		} else if (Color4* col4 = std::get_if<Color4>(&value.value)) {
			const Color4& c = std::get<Color4>(current);
			value.value = Color4(merge(col4->r, c.r, 0), merge(col4->g, c.g, 1), merge(col4->b, c.b, 2), merge(col4->a, c.a, 3));
		}
	}
}
