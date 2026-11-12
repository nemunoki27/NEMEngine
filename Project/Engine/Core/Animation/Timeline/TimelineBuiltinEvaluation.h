#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineClipInstance.h"
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSampling.h>

// c++
#include <functional>

namespace Engine {

	// 子Timelineを含む評価を一括して混合する
	void BeginBuiltinTimelineEvaluation(ECSWorld& world);
	void CommitBuiltinTimelineSamples(ECSWorld& world, SystemContext& context);
	void RetireBuiltinTimelineSamples(ECSWorld& world, const TimelineControlOwner& owner);
	void FlushBuiltinTimelineEvaluation(ECSWorld& world, SystemContext& context);
	void DeferBuiltinTimelineExit(ECSWorld& world, std::function<void()> cleanup);
	void UpdateTimelineAttachmentsForPhysics(ECSWorld& world, SystemContext& context);
	// 時間倍率が0でも実時間による解除を進める
	void UpdateBuiltinTimelineRealtime(ECSWorld& world, float unscaledDeltaTime);
	void QueueTimelineSkinnedSample(TimelineEvaluationContext& context, const SkinnedAnimationSample& sample);
	void QueueTimelinePropertySample(TimelineEvaluationContext& context, const AnimationPropertyDescriptor& descriptor,
		const AnimationPropertyValue& baseline, const AnimationPropertyValue& value);
	// 重複Clipで同じ復元値を共有する
	AnimationPropertyValue CaptureTimelinePropertyBase(TimelineEvaluationContext& context,
		const AnimationPropertyDescriptor& descriptor);
	bool TryGetTimelinePropertyBase(const ECSWorld& world, Entity entity,
		const AnimationPropertyBinding& binding, AnimationPropertyValue& value);
	void RestoreTimelinePropertyBase(TimelineEvaluationContext& context,
		const AnimationPropertyDescriptor& descriptor, bool restore);
}
