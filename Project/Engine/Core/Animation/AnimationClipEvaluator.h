#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clip/AnimationClipAsset.h>
#include <Engine/Core/Animation/Property/AnimationPropertyRegistry.h>

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

		AnimationPropertyBinding binding;
		AnimationPropertyValue value;
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

		static bool EvaluateTrack(const AnimationCurveTrack& track, float time, AnimationPropertyValue& outValue);
		static bool ApplyTrack(ECSWorld& world, const Entity& entity, const AnimationCurveTrack& track,
			float time, const AnimationPropertyValue* baseValueOrNull);
		static void ApplyClip(ECSWorld& world, const Entity& entity, const AnimationClipAsset& clip,
			float time, std::span<const AnimationPreviewBaseValue> baseValues);
	};
} // Engine
