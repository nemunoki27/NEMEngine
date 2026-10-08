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

	// 同じ値型の成分を許容誤差内で比較する
	bool ApproxEqualValue(const AnimationPropertyValue& lhs, const AnimationPropertyValue& rhs);
	// キーのない軸へ外部編集値を取り込む
	AnimationPropertyValue MergeEditedBaseValue(
		const AnimationCurveTrack& track, const AnimationPropertyValue& current, const AnimationPropertyValue& base);
	// 適用方法の表示名を取得する
	const char* ApplyModeLabel(AnimationApplyMode mode);
	// 値型ごとのCurve生成先を列挙する
	std::vector<CurveBakeTarget> BuildBakeTargets(const AnimationCurveTrack& track);
	// PropertyとSubMeshからTrackの表示名を作る
	std::string BuildTrackLabel(const AnimationCurveTrack& track, ECSWorld* world, const Entity& entity);
	// 検出した描画次元の表示名を取得する
	const char* DetectedDimensionText(AnimationClipDetectedDimension dimension);
	// 2D用のTransform Propertyか判定する
	bool Is2DTransformProperty(std::string_view propertyPath);
	// 3D用のTransform Propertyか判定する
	bool Is3DTransformProperty(std::string_view propertyPath);
	// 重複を除いてキーの時刻を集める
	void CollectKeyTimes(std::span<const CurveChannel> channels, std::vector<float>& outTimes);
	// 指定時刻のキーの補間方法を取得する
	CurveInterpolationMode FindKeyInterpolationAt(const CurveChannel& channel, float time);
	// Quaternionを回転軸と角度へ分解する
	CurveQuaternionAxisKey MakeAxisKeyFromQuaternion(const Quaternion& rotation, float& outAngleDegrees);
	// Trackを回転軸と角度のCurveへ変換する
	CurveQuaternion BuildQuaternionEditorCurve(const AnimationCurveTrack& track);
	// 編集した回転軸と角度をTrackへ戻す
	void StoreQuaternionEditorCurve(const CurveQuaternion& curve, AnimationCurveTrack& track);
	// キーを空にしてChannelの既定値を設定する
	void FillChannel(CurveChannel& channel, float value);
	// 現在値をTrackの既定値へ取り込む
	void SetupTrackInitialValue(AnimationCurveTrack& track, const AnimationPropertyValue& value);
	// 指定時刻のキーを許容誤差内で探す
	bool FindKeyIndexAtTime(const CurveChannel& channel, float time, uint32_t& outIndex);
	// 選択ChannelをRGBとして編集できるか判定する
	bool CanDrawColorRgbKeyEditor(const AnimationCurveTrack& track, uint32_t selectedChannelIndex);
	// 回転軸と角度を持つTrackか判定する
	bool IsQuaternionAxisAngleTrack(const AnimationCurveTrack& track);
	// 不足する回転軸キーを補って取得する
	CurveQuaternionAxisKey& GetQuaternionAxisKeyForEdit(AnimationCurveTrack& track, uint32_t keyIndex);
	// 回転軸キーの代表軸を取得する
	float GetPrimaryAxisValue(const CurveQuaternionAxisKey& axisKey);
	// 回転軸と対応キーを時刻順に並べる
	void SortQuaternionAxisKeys(AnimationCurveTrack& track);
	// RGBのキーを色として編集する
	bool DrawColorKeyValueEditor(AnimationCurveTrack& track, uint32_t selectedChannelIndex, float time);
}
