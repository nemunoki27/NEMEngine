#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipEditTypes.h"
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::AnimationClipEditorUtility {

	// 編集用のキーと値を解決する
bool ApproxEqualValue(const AnimationPropertyValue& lhs, const AnimationPropertyValue& rhs);
	// 編集用のキーと値を解決する
AnimationPropertyValue MergeEditedBaseValue(const AnimationCurveTrack& track,
		const AnimationPropertyValue& current, const AnimationPropertyValue& base);
	// 編集用のキーと値を解決する
const char* ApplyModeLabel(AnimationApplyMode mode);
	// 編集用のキーと値を解決する
std::vector<CurveBakeTarget> BuildBakeTargets(const AnimationCurveTrack& track);
	// 編集用のキーと値を解決する
std::string BuildTrackLabel(const AnimationCurveTrack& track, ECSWorld* world, const Entity& entity);
	// 編集用のキーと値を解決する
const char* DetectedDimensionText(AnimationClipDetectedDimension dimension);
	// 編集用のキーと値を解決する
bool Is2DTransformProperty(std::string_view propertyPath);
	// 編集用のキーと値を解決する
bool Is3DTransformProperty(std::string_view propertyPath);
	// 編集用のキーと値を解決する
void CollectKeyTimes(std::span<const CurveChannel> channels, std::vector<float>& outTimes);
	// 編集用のキーと値を解決する
CurveInterpolationMode FindKeyInterpolationAt(const CurveChannel& channel, float time);
	// 編集用のキーと値を解決する
CurveQuaternionAxisKey MakeAxisKeyFromQuaternion(const Quaternion& rotation, float& outAngleDegrees);
	// 編集用のキーと値を解決する
CurveQuaternion BuildQuaternionEditorCurve(const AnimationCurveTrack& track);
	// 編集用のキーと値を解決する
void StoreQuaternionEditorCurve(const CurveQuaternion& curve, AnimationCurveTrack& track);
	// 編集用のキーと値を解決する
void FillChannel(CurveChannel& channel, float value);
	// 編集用のキーと値を解決する
void SetupTrackInitialValue(AnimationCurveTrack& track, const AnimationPropertyValue& value);
	// 編集用のキーと値を解決する
bool FindKeyIndexAtTime(const CurveChannel& channel, float time, uint32_t& outIndex);
	// 編集用のキーと値を解決する
bool CanDrawColorRgbKeyEditor(const AnimationCurveTrack& track, uint32_t selectedChannelIndex);
	// 編集用のキーと値を解決する
bool IsQuaternionAxisAngleTrack(const AnimationCurveTrack& track);
	// 編集用のキーと値を解決する
CurveQuaternionAxisKey& GetQuaternionAxisKeyForEdit(AnimationCurveTrack& track, uint32_t keyIndex);
	// 編集用のキーと値を解決する
float GetPrimaryAxisValue(const CurveQuaternionAxisKey& axisKey);
	// 編集用のキーと値を解決する
void SortQuaternionAxisKeys(AnimationCurveTrack& track);
	// 編集用のキーと値を解決する
bool DrawColorKeyValueEditor(AnimationCurveTrack& track, uint32_t selectedChannelIndex, float time);
}
