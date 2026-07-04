#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;
	struct Entity;

	//============================================================================
	//	AnimationClipEvaluator structures
	//============================================================================
	struct AnimationPreviewBaseValue {

		// Preview開始時に戻すための元値
		AnimationPropertyBinding binding;
		AnimationPropertyValue value;
		// Preview開始時に値が存在したか、material override未設定はfalseで復元時に除去する
		bool present = true;
	};

	struct AnimationResolvedTime {

		// 実際にClip内を評価する時刻
		float clipTime = 0.0f;
		// LoopBridge区間にいるか
		bool inLoopBridge = false;
		// LoopBridge内の0-1補間率
		float bridgeT = 0.0f;
	};

	// Clipを書き込まずに評価した1プロパティ分の最終値、クロスフェード合成に使う
	struct AnimationEvaluatedValue {

		AnimationPropertyBinding binding;
		AnimationPropertyValue value;
	};

	//============================================================================
	//	AnimationClipEvaluator class
	//	Clipの評価とComponentへの適用だけを担当する
	//============================================================================
	class AnimationClipEvaluator {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// Track単体を指定時刻で評価する
		static bool EvaluateTrack(const AnimationCurveTrack& track, float time, AnimationPropertyValue& outValue);
		// Resolve済み時刻を使い、LoopBridgeも含めてTrackを評価する
		static bool EvaluateTrack(const AnimationCurveTrack& track, const AnimationResolvedTime& time,
			const AnimationClipAsset& clip, AnimationPropertyValue& outValue);
		// 再生時間をClip内時刻へ変換する
		static AnimationResolvedTime ResolveClipEvaluationTime(const AnimationClipAsset& clip, float playbackTime);
		// LoopBridgeの0-1補間率を指定モードで曲げて返す、実行時のstate側補間を使う合成に使う
		static float BridgeInterp(float t, CurveInterpolationMode mode);
		// LoopBridge込みの再生上限時間を返す
		static float GetPlaybackDuration(const AnimationClipAsset& clip);
		// 1Trackを対象EntityのComponentへ反映する
		static bool ApplyTrack(ECSWorld& world, const Entity& entity, const AnimationCurveTrack& track,
			const AnimationClipAsset& clip, const AnimationResolvedTime& time, const AnimationPropertyValue* baseValueOrNull);
		// Clip全体を対象Entityへ反映する
		static void ApplyClip(ECSWorld& world, const Entity& entity, const AnimationClipAsset& clip,
			float time, std::span<const AnimationPreviewBaseValue> baseValues);
		// Clip全体を書き込まず、各プロパティの最終値だけを評価して返す、時刻解決は呼び出し側が行う
		static void EvaluateClipValues(ECSWorld& world, const Entity& entity, const AnimationClipAsset& clip,
			const AnimationResolvedTime& time, std::span<const AnimationPreviewBaseValue> baseValues,
			std::vector<AnimationEvaluatedValue>& outValues);
		// from/to2つの評価済み値をweightで合成する、片側に無いプロパティはbaseへ寄せる
		static void BlendValues(std::span<const AnimationEvaluatedValue> fromValues,
			std::span<const AnimationEvaluatedValue> toValues, std::span<const AnimationPreviewBaseValue> baseValues,
			float weight, std::vector<AnimationEvaluatedValue>& outValues);
		// 評価済み値を対象EntityのComponentへ書き込む
		static void WriteValues(ECSWorld& world, const Entity& entity, std::span<const AnimationEvaluatedValue> values);
		// キーの無いチャネルは書き込まず、対象Entityの現在値のまま残す(スクリプト等が持つ未キー成分を保持する)
		// 例: PosYのみアニメするクリップで、スクリプトが動かすXZを上書きしないようにする
		static void PreserveUnkeyedChannels(ECSWorld& world, const Entity& entity,
			const AnimationClipAsset& clip, std::vector<AnimationEvaluatedValue>& values);
		// Transformの位置/回転をbaseValuesの基準姿勢を正面として相対化する、向き相対クリップ用
		// clipNeutralはクリップ開始姿勢(t=0)で、作成時のEntity位置に依存しないようここからの差分だけを基準へ適用する
		static void ComposeRelativeTransform(std::vector<AnimationEvaluatedValue>& values,
			std::span<const AnimationPreviewBaseValue> baseValues,
			std::span<const AnimationEvaluatedValue> clipNeutral);
	};
} // Engine
