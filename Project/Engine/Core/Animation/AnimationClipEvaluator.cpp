#include "AnimationClipEvaluator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

//============================================================================
//	AnimationClipEvaluator classMethods
//============================================================================

namespace {

	bool SameBinding(const Engine::AnimationPropertyBinding& lhs, const Engine::AnimationPropertyBinding& rhs) {

		return lhs.componentName == rhs.componentName && lhs.propertyPath == rhs.propertyPath;
	}

	const Engine::AnimationPropertyValue* FindBaseValue(
		const Engine::AnimationCurveTrack& track,
		std::span<const Engine::AnimationPreviewBaseValue> baseValues) {

		for (const Engine::AnimationPreviewBaseValue& baseValue : baseValues) {
			if (SameBinding(baseValue.binding, track.binding)) {
				return &baseValue.value;
			}
		}
		return nullptr;
	}

	bool ReadChannels(const Engine::AnimationCurveTrack& track, float time, float* values, uint32_t valueCount) {

		if (track.channels.size() < valueCount) {
			return false;
		}
		for (uint32_t i = 0; i < valueCount; ++i) {
			values[i] = track.channels[i].Evaluate(time);
		}
		return true;
	}

	bool CombineValue(const Engine::AnimationPropertyValue& baseValue,
		const Engine::AnimationPropertyValue& curveValue,
		Engine::AnimationApplyMode mode,
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

		// QuaternionのAdd/Multiplyは初期UIでは選ばせない。データだけ残っていた場合は安全側で適用しない。
		return false;
	}
}

bool Engine::AnimationClipEvaluator::EvaluateTrack(const AnimationCurveTrack& track,
	float time, AnimationPropertyValue& outValue) {

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

bool Engine::AnimationClipEvaluator::ApplyTrack(ECSWorld& world, const Entity& entity,
	const AnimationCurveTrack& track, float time, const AnimationPropertyValue* baseValueOrNull) {

	const AnimationPropertyDescriptor* desc = AnimationPropertyRegistry::GetInstance().Find(
		track.binding.componentName, track.binding.propertyPath);
	if (!desc || !desc->hasComponent || !desc->setValue || !desc->hasComponent(world, entity)) {
		return false;
	}

	AnimationPropertyValue curveValue{};
	if (!EvaluateTrack(track, time, curveValue)) {
		return false;
	}

	AnimationPropertyValue finalValue{};
	if (track.applyMode == AnimationApplyMode::Override) {
		finalValue = curveValue;
	} else {
		if (!baseValueOrNull || !CombineValue(*baseValueOrNull, curveValue, track.applyMode, finalValue)) {
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

	const float safeTime = (std::clamp)(time, 0.0f, (std::max)(clip.duration, 0.001f));
	for (const AnimationCurveTrack& track : clip.curveTracks) {

		const AnimationPropertyValue* baseValue = FindBaseValue(track, baseValues);
		ApplyTrack(world, entity, track, safeTime, baseValue);
	}
}
