#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipEvaluator.h"

namespace Engine::AnimationValueOperations {

	bool SameBinding(const AnimationPropertyBinding& lhs, const AnimationPropertyBinding& rhs);

	const AnimationPropertyValue* FindBaseValue( const AnimationCurveTrack& track,
		std::span<const AnimationPreviewBaseValue> baseValues);

	const AnimationPropertyValue* FindBaseValueByBinding( std::span<const AnimationPreviewBaseValue> baseValues,
		const AnimationPropertyBinding& binding);

	const AnimationPropertyValue* FindEvaluatedValue( std::span<const AnimationEvaluatedValue> values,
		const AnimationPropertyBinding& binding);

	bool CombineValue(const AnimationPropertyValue& baseValue, const AnimationPropertyValue& curveValue,
		AnimationApplyMode mode, QuaternionMultiplyOrder quaternionOrder, AnimationPropertyValue& out);

	bool LerpValue(const AnimationPropertyValue& from,
		const AnimationPropertyValue& to, float t, AnimationPropertyValue& out);
}
