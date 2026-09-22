#include "AnimationValueOperations.h"

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationValueOperations {

	bool SameBinding(const Engine::AnimationPropertyBinding& lhs, const Engine::AnimationPropertyBinding& rhs) {

		return lhs.componentName == rhs.componentName && lhs.propertyPath == rhs.propertyPath;
	}

	const Engine::AnimationPropertyValue* FindBaseValue( const Engine::AnimationCurveTrack& track,
		std::span<const Engine::AnimationPreviewBaseValue> baseValues) {

		// Add/MultiplyはPreview開始時の値を基準にするため、Trackと同じbindingを探す
		for (const Engine::AnimationPreviewBaseValue& baseValue : baseValues) {
			if (SameBinding(baseValue.binding, track.binding)) {
				return &baseValue.value;
			}
		}
		return nullptr;
	}

	const Engine::AnimationPropertyValue* FindBaseValueByBinding(
		std::span<const Engine::AnimationPreviewBaseValue> baseValues,
		const Engine::AnimationPropertyBinding& binding) {

		// クロスフェードで片側に無いプロパティをbaseへ寄せるため同じbindingを探す
		for (const Engine::AnimationPreviewBaseValue& baseValue : baseValues) {
			if (SameBinding(baseValue.binding, binding)) {
				return &baseValue.value;
			}
		}
		return nullptr;
	}

	const Engine::AnimationPropertyValue* FindEvaluatedValue( std::span<const Engine::AnimationEvaluatedValue> values,
		const Engine::AnimationPropertyBinding& binding) {

		// 評価済み値リストから同じbindingの値を引く
		for (const Engine::AnimationEvaluatedValue& value : values) {
			if (SameBinding(value.binding, binding)) {
				return &value.value;
			}
		}
		return nullptr;
	}

	bool CombineValue(const Engine::AnimationPropertyValue& baseValue, const Engine::AnimationPropertyValue& curveValue,
		Engine::AnimationApplyMode mode, Engine::QuaternionMultiplyOrder quaternionOrder,
		Engine::AnimationPropertyValue& out) {

		if (mode == Engine::AnimationApplyMode::Override) {
			out = curveValue;
			return true;
		}

		// Add/MultiplyはPreview開始時に記録した基準値だけを使う
		// 毎フレーム現在値へ積み上げると、ScrubやPlayで値が破綻する
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
					Engine::Quaternion::Normalize(*base * *curve) : Engine::Quaternion::Normalize(*curve * *base);
				return true;
			}
		}

		return false;
	}

	bool LerpValue(const Engine::AnimationPropertyValue& from,
		const Engine::AnimationPropertyValue& to, float t, Engine::AnimationPropertyValue& out) {

		// LoopBridgeで使う型別補間
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
