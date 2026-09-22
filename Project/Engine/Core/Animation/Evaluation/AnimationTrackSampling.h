#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipEvaluator.h"
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

namespace Engine::AnimationTrackSampling {

	bool ReadChannels(const AnimationCurveTrack& track, float time, float* values, uint32_t valueCount);

	bool IsQuaternionAxisAngleTrack(const AnimationCurveTrack& track);

	CurveQuaternionAxisKey GetQuaternionAxisKey(const AnimationCurveTrack& track, uint32_t keyIndex);

	uint32_t FindQuaternionAxisIndex(const AnimationCurveTrack& track, float time);

	bool EvaluateQuaternionAxisAngleTrack(const AnimationCurveTrack& track, float time,
		AnimationPropertyValue& outValue);

	bool HasAnyKey(const AnimationCurveTrack& track);

	bool ReadValueChannels(const AnimationPropertyValue& value, float* values, uint32_t valueCount);

	bool ReadChannelsWithFallback(const AnimationCurveTrack& track, float time,
		const AnimationPropertyValue& fallback, float* values, uint32_t valueCount);

	bool EvaluateTrackWithFallback(const AnimationCurveTrack& track, float time,
		const AnimationPropertyValue& fallback, AnimationPropertyValue& outValue);

	bool EvaluateTrackWithFallback(const AnimationCurveTrack& track,
		const AnimationResolvedTime& time, const AnimationClipAsset& clip,
		const AnimationPropertyValue& fallback, AnimationPropertyValue& outValue);
}
