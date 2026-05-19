#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>

// c++
#include <span>

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
	};

	struct AnimationResolvedTime {

		// 実際にClip内を評価する時刻
		float clipTime = 0.0f;
		// LoopBridge区間にいるか
		bool inLoopBridge = false;
		// LoopBridge内の0-1補間率
		float bridgeT = 0.0f;
	};

	//============================================================================
	//	AnimationClipEvaluator class
	//	Clipの評価とComponentへの適用だけを担当する
	//============================================================================
	class AnimationClipEvaluator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Track単体を指定時刻で評価する
		static bool EvaluateTrack(const AnimationCurveTrack& track, float time, AnimationPropertyValue& outValue);
		// Resolve済み時刻を使い、LoopBridgeも含めてTrackを評価する
		static bool EvaluateTrack(const AnimationCurveTrack& track, const AnimationResolvedTime& time,
			const AnimationClipAsset& clip, AnimationPropertyValue& outValue);
		// 再生時間をClip内時刻へ変換する
		static AnimationResolvedTime ResolveClipEvaluationTime(const AnimationClipAsset& clip, float playbackTime);
		// LoopBridge込みの再生上限時間を返す
		static float GetPlaybackDuration(const AnimationClipAsset& clip);
		// 1Trackを対象EntityのComponentへ反映する
		static bool ApplyTrack(ECSWorld& world, const Entity& entity, const AnimationCurveTrack& track,
			const AnimationClipAsset& clip, const AnimationResolvedTime& time, const AnimationPropertyValue* baseValueOrNull);
		// Clip全体を対象Entityへ反映する
		static void ApplyClip(ECSWorld& world, const Entity& entity, const AnimationClipAsset& clip,
			float time, std::span<const AnimationPreviewBaseValue> baseValues);
	};
} // Engine
