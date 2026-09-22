#include "AnimationClipEvaluator.h"
#include "AnimationValueOperations.h"

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::AnimationValueOperations;

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
			}
		} else if (value.binding.propertyPath == "localRotation") {
			if (Quaternion* v = std::get_if<Quaternion>(&value.value)) {
				value.value = Quaternion::Normalize(baseRot * (neutralRotInverse * *v));
			}
		} else if (value.binding.propertyPath == "localPos2D") {
			if (Vector2* v = std::get_if<Vector2>(&value.value)) {
				const float ox = v->x - neutralPos2D.x;
				const float oy = v->y - neutralPos2D.y;
				value.value = Vector2(basePos2D.x + (ox * cosZ - oy * sinZ),
					basePos2D.y + (ox * sinZ + oy * cosZ));
			}
		} else if (value.binding.propertyPath == "localRotationZ") {
			if (float* v = std::get_if<float>(&value.value)) {
				value.value = baseAngleZ + (*v - neutralAngleZ);
			}
		}
	}
}
