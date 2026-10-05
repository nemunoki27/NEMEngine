#include "AnimationClipEvaluator.h"
#include "AnimationValueOperations.h"
#include "AnimationTrackSampling.h"

// c++
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Engine::AnimationValueOperations;

void Engine::AnimationClipEvaluator::ComposeValues(std::span<const AnimationContribution> contributions,
	std::span<const AnimationPreviewBaseValue> baseValues, std::vector<AnimationEvaluatedValue>& outValues) {

	outValues.clear();
	// 回転の加算順を優先度で固定する
	std::vector<const AnimationContribution*> additive;
	for (const auto& contribution : contributions) {

		if (contribution.additive) additive.push_back(&contribution);
	}
	std::stable_sort(additive.begin(), additive.end(), [](const auto* lhs, const auto* rhs) {

		return lhs->priority < rhs->priority;
	});
	for (const AnimationContribution& first : contributions) {

		const auto& binding = first.evaluated.binding;
		if (FindEvaluatedValue(outValues, binding)) continue;
		const AnimationPropertyValue* base = FindBaseValueByBinding(baseValues, binding);
		if (!base) continue;
		const uint32_t count = GetAnimationValueTypeChannelCount(binding.valueType);
		float baseChannels[4]{};
		if (!AnimationTrackSampling::ReadValueChannels(*base, baseChannels, count)) continue;
		float result[4]{};
		uint8_t channelMask = 0;
		// 成分ごとに最高優先度のOverrideを集める
		for (uint32_t channel = 0; channel < count; ++channel) {

			int32_t priority = (std::numeric_limits<int32_t>::min)();
			for (const AnimationContribution& contribution : contributions) {
				if (SameBinding(binding, contribution.evaluated.binding) && contribution.evaluated.value.index() == base->index() && !contribution.additive &&
					std::isfinite(contribution.weight) && contribution.weight > 0.0f &&
					(contribution.evaluated.channelMask & (1u << channel))) {
					priority = (std::max)(priority, contribution.priority);
				}
			}
			float totalWeight = 0.0f;
			float sum = 0.0f;
			float reference[4]{};
			bool referenceSet = false;
			for (const AnimationContribution& contribution : contributions) {

				if (!SameBinding(binding, contribution.evaluated.binding) || contribution.evaluated.value.index() != base->index() || contribution.additive ||
					contribution.priority != priority || !std::isfinite(contribution.weight) ||
					contribution.weight <= 0.0f || !(contribution.evaluated.channelMask & (1u << channel))) continue;
				float values[4]{};
				if (!AnimationTrackSampling::ReadValueChannels(contribution.evaluated.value, values, count)) continue;
				float weight = std::clamp(contribution.weight, 0.0f, 1.0f);
				// Quaternionの符号を基準姿勢に揃える
				if (binding.valueType == AnimationValueType::Quaternion) {
					if (!referenceSet) {
						std::copy_n(values, 4, reference);
						referenceSet = true;
					}
					float dot = 0.0f;
					for (uint32_t i = 0; i < 4; ++i) dot += values[i] * reference[i];
					if (dot < 0.0f) values[channel] = -values[channel];
				}
				sum += values[channel] * weight;
				totalWeight += weight;
			}
			float baseChannel = baseChannels[channel];
			if (referenceSet) {
				float dot = 0.0f;
				for (uint32_t i = 0; i < 4; ++i) dot += baseChannels[i] * reference[i];
				if (dot < 0.0f) baseChannel = -baseChannel;
			}
			result[channel] = totalWeight > 1.0f ? sum / totalWeight : sum + baseChannel * (1.0f - totalWeight);
			if (totalWeight > 0.0f) channelMask |= static_cast<uint8_t>(1u << channel);
		}
		AnimationEvaluatedValue composed{};
		composed.binding = binding;
		if (!AnimationTrackSampling::WriteValueChannels(binding.valueType, std::span<const float>(result, count), composed.value)) continue;
		// Additiveは開始姿勢との差分を加える
		for (const auto* source : additive) {

			const AnimationContribution& contribution = *source;
			if (!contribution.additive || !SameBinding(binding, contribution.evaluated.binding) || contribution.evaluated.value.index() != base->index() ||
				!std::isfinite(contribution.weight) || contribution.weight <= 0.0f) continue;
			const float weight = std::clamp(contribution.weight, 0.0f, 1.0f);
			if (binding.valueType == AnimationValueType::Quaternion) {

				const auto* origin = std::get_if<Quaternion>(base);
				const auto* value = std::get_if<Quaternion>(&contribution.evaluated.value);
				if (!origin || !value) continue;
				const Quaternion delta = Quaternion::Normalize(Quaternion::Inverse(*origin) * *value);
				AnimationPropertyValue weighted;
				if (!LerpValue(Quaternion(0.0f, 0.0f, 0.0f, 1.0f), delta, weight, weighted)) continue;
				composed.value = Quaternion::Normalize(std::get<Quaternion>(composed.value) * std::get<Quaternion>(weighted));
				channelMask = 0x0f;
				continue;
			}
			float values[4]{};
			if (!AnimationTrackSampling::ReadValueChannels(contribution.evaluated.value, values, count)) continue;
			float differences[4]{};
			for (uint32_t channel = 0; channel < count; ++channel) {
				if (contribution.evaluated.channelMask & (1u << channel)) {
					differences[channel] = (values[channel] - baseChannels[channel]) * weight;
				}
			}
			AnimationPropertyValue delta;
			if (!AnimationTrackSampling::WriteValueChannels(binding.valueType, std::span<const float>(differences, count), delta)) continue;
			AnimationPropertyValue combined;
			if (!CombineValue(composed.value, delta, AnimationApplyMode::Add, QuaternionMultiplyOrder::BaseThenCurve, combined)) continue;
			composed.value = std::move(combined);
			channelMask |= contribution.evaluated.channelMask;
		}
		composed.channelMask = channelMask;
		if (channelMask) outValues.emplace_back(std::move(composed));
	}
}

void Engine::AnimationClipEvaluator::BlendValues(std::span<const AnimationEvaluatedValue> fromValues,
	std::span<const AnimationEvaluatedValue> toValues, std::span<const AnimationPreviewBaseValue> baseValues,
	float weight, std::vector<AnimationEvaluatedValue>& outValues) {

	outValues.clear();
	const float w = std::clamp(weight, 0.0f, 1.0f);

	// from側を基準に合成し、to側だけが持つプロパティは後で足す
	for (const AnimationEvaluatedValue& fromValue : fromValues) {

		const AnimationPropertyValue* toValue = FindEvaluatedValue(toValues, fromValue.binding);
		const AnimationPropertyValue* baseValue = FindBaseValueByBinding(baseValues, fromValue.binding);
		// to側に無いプロパティはbaseへ、baseも無ければfrom値のまま留める
		const AnimationPropertyValue& target = toValue ? *toValue : (baseValue ? *baseValue : fromValue.value);
		AnimationEvaluatedValue blended{};
		blended.binding = fromValue.binding;
		blended.channelMask = fromValue.channelMask;
		for (const auto& value : toValues) {
			if (SameBinding(value.binding, fromValue.binding)) blended.channelMask |= value.channelMask;
		}
		if (!LerpValue(fromValue.value, target, w, blended.value)) {
			blended.value = w < 0.5f ? fromValue.value : target;
		}
		outValues.emplace_back(std::move(blended));
	}

	// to側だけが持つプロパティはbaseからto値へ寄せる
	for (const AnimationEvaluatedValue& toValue : toValues) {

		if (FindEvaluatedValue(fromValues, toValue.binding)) {
			continue;
		}
		const AnimationPropertyValue* baseValue = FindBaseValueByBinding(baseValues, toValue.binding);
		const AnimationPropertyValue& source = baseValue ? *baseValue : toValue.value;
		AnimationEvaluatedValue blended{};
		blended.binding = toValue.binding;
		blended.channelMask = toValue.channelMask;
		if (!LerpValue(source, toValue.value, w, blended.value)) {
			blended.value = w < 0.5f ? source : toValue.value;
		}
		outValues.emplace_back(std::move(blended));
	}
}

void Engine::AnimationClipEvaluator::ComposeRelativeTransform(std::vector<AnimationEvaluatedValue>& values,
	std::span<const AnimationPreviewBaseValue> baseValues, std::span<const AnimationEvaluatedValue> clipNeutral) {

	// 基準姿勢をbaseValuesから取り出す、無ければ原点と無回転とみなす
	Vector3 basePos{};
	Quaternion baseRot(0.0f, 0.0f, 0.0f, 1.0f);
	Vector2 basePos2D{};
	float baseAngleZ = 0.0f;
	for (const AnimationPreviewBaseValue& base : baseValues) {

		if (base.binding.componentName != "Transform") {
			continue;
		}
		if (base.binding.propertyPath == "localPos") {
			if (const Vector3* v = std::get_if<Vector3>(&base.value)) { basePos = *v; }
		} else if (base.binding.propertyPath == "localRotation") {
			if (const Quaternion* v = std::get_if<Quaternion>(&base.value)) { baseRot = *v; }
		} else if (base.binding.propertyPath == "localPos2D") {
			if (const Vector2* v = std::get_if<Vector2>(&base.value)) { basePos2D = *v; }
		} else if (base.binding.propertyPath == "localRotationZ") {
			if (const float* v = std::get_if<float>(&base.value)) { baseAngleZ = *v; }
		}
	}

	// クリップ開始姿勢(t=0)を中立として取り出す、作成時のEntity位置に依存しないようここからの差分だけを使う
	Vector3 neutralPos{};
	Quaternion neutralRot(0.0f, 0.0f, 0.0f, 1.0f);
	Vector2 neutralPos2D{};
	float neutralAngleZ = 0.0f;
	for (const AnimationEvaluatedValue& neutral : clipNeutral) {

		if (neutral.binding.componentName != "Transform") {
			continue;
		}
		if (neutral.binding.propertyPath == "localPos") {
			if (const Vector3* v = std::get_if<Vector3>(&neutral.value)) { neutralPos = *v; }
		} else if (neutral.binding.propertyPath == "localRotation") {
			if (const Quaternion* v = std::get_if<Quaternion>(&neutral.value)) { neutralRot = *v; }
		} else if (neutral.binding.propertyPath == "localPos2D") {
			if (const Vector2* v = std::get_if<Vector2>(&neutral.value)) { neutralPos2D = *v; }
		} else if (neutral.binding.propertyPath == "localRotationZ") {
			if (const float* v = std::get_if<float>(&neutral.value)) { neutralAngleZ = *v; }
		}
	}

	const Matrix4x4 baseRotMatrix = Quaternion::MakeRotateMatrix(baseRot);
	const Quaternion neutralRotInverse = Quaternion::Inverse(neutralRot);
	constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
	const float cosZ = std::cos(baseAngleZ * kDegToRad);
	const float sinZ = std::sin(baseAngleZ * kDegToRad);

	// クリップ開始姿勢からの差分を基準姿勢を正面として合成する
	for (AnimationEvaluatedValue& value : values) {

		if (value.binding.componentName != "Transform") {
			continue;
		}
		if (value.binding.propertyPath == "localPos") {
			if (Vector3* v = std::get_if<Vector3>(&value.value)) {
				value.value = basePos + Vector3::TransferNormal(*v - neutralPos, baseRotMatrix);
				// 基準回転で差分が全軸へ広がる
				value.channelMask = 0x07;
			}
		} else if (value.binding.propertyPath == "localRotation") {
			if (Quaternion* v = std::get_if<Quaternion>(&value.value)) {
				value.value = Quaternion::Normalize(baseRot * (neutralRotInverse * *v));
				value.channelMask = 0x0f;
			}
		} else if (value.binding.propertyPath == "localPos2D") {
			if (Vector2* v = std::get_if<Vector2>(&value.value)) {
				const float ox = v->x - neutralPos2D.x;
				const float oy = v->y - neutralPos2D.y;
				value.value = Vector2(basePos2D.x + (ox * cosZ - oy * sinZ),
					basePos2D.y + (ox * sinZ + oy * cosZ));
				value.channelMask = 0x03;
			}
		} else if (value.binding.propertyPath == "localRotationZ") {
			if (float* v = std::get_if<float>(&value.value)) {
				value.value = baseAngleZ + (*v - neutralAngleZ);
			}
		}
	}
}
