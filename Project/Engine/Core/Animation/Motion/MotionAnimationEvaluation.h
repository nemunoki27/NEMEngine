#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>

namespace Engine {
	struct MotionAnimationSample {
		Vector3 position{}, tangent{};
		Quaternion rotation{};
		uint64_t cycles = 0;
		bool finished = false;
	};

	void SetMotionAnimationRotationEdit(MotionAnimationSettings& settings, SimpleAnimationRotationMode mode);
	double GetMotionAnimationDuration(const MotionAnimationSettings& settings);
	MotionAnimationSample EvaluateMotionAnimation(const MotionAnimationSettings& settings,
		const MotionAnimationRuntime& runtime, double time);
	void AccumulateMotionAnimationEvents(const MotionAnimationSettings& settings, MotionAnimationRuntime& runtime,
		double previousTime, double currentTime);
	std::vector<Vector3> GetMotionAnimationDebugPoints(ECSWorld& world, Entity entity);
}
