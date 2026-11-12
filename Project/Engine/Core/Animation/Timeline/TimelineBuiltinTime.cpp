#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	struct TimeLease {

		Entity target{};
		TimelineControlOwner owner;
		AnimationPropertyDescriptor descriptor;
		AnimationPropertyValue baseline;
		float remaining = 0;
		bool expired = false, active = true;
	};
	struct TimeLeaseStorage {

		std::vector<std::shared_ptr<TimeLease>> values;
	};

	// ローカルと全体の時計を共通のプロパティ所有へ接続する
	AnimationPropertyDescriptor TimeDescriptor(bool global) {

		AnimationPropertyDescriptor descriptor;
		descriptor.componentName = global ? "GlobalTime" : "TimeScale";
		descriptor.propertyPath = "timeScale";
		descriptor.valueType = AnimationValueType::Float;
		descriptor.getValue = [global](ECSWorld& world, const Entity& entity, AnimationPropertyValue& value) {
			if (global) { value = ManagedScriptRuntime::GetRuntimeTimeScale(); return true; }
			const auto* component = world.TryGetComponentForBinding<TimeScaleComponent>(entity);
			if (!component) return false;
			value = component->timeScale; return true;
		};
		descriptor.setValue = [global](ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) {
			const auto* number = std::get_if<float>(&value);
			if (!number) return false;
			if (global) { ManagedScriptRuntime::SetRuntimeTimeScale(*number); return true; }
			auto* component = world.TryGetComponentForBinding<TimeScaleComponent>(entity);
			if (!component) return false;
			component->timeScale = *number;
			world.MarkComponentModified<TimeScaleComponent>(entity); return true;
		};
		return descriptor;
	}

	class TimeClip final : public ITimelineClipInstance {
	public:

		explicit TimeClip(const TimelineClip& clip) : scale_(clip.data.value("scale", 0.25f)),
			hold_(clip.data.value("holdSeconds", 0.1f)), global_(clip.data.value("global", false)),
			hitStop_(clip.data.value("hitStop", false)), descriptor_(TimeDescriptor(global_)) {

			if (!std::isfinite(scale_) || scale_ < 0 || !std::isfinite(hold_) || hold_ < 0)
				throw std::invalid_argument("時間倍率と解除時間は有限の0以上にしてください");
			if (scale_ == 0) hitStop_ = true;
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (!context.world.IsAlive(context.target)) context.target = context.owner;
			AnimationPropertyValue value;
			if (!descriptor_.getValue(context.world, context.target, value)) {
				diagnostic = "対象にTimeScaleを追加してください"; return false;
			}
			AcquireTimelineControl(context.world, context.target, descriptor_.componentName, descriptor_.propertyPath, context.control);
			baseline_ = CaptureTimelinePropertyBase(context, descriptor_);
			if (hitStop_ && !context.isScrubbing && context.mode == TimelineEvaluationMode::Playback) {
				lease_ = std::make_shared<TimeLease>();
				lease_->target = context.target;
				lease_->owner = context.control;
				lease_->descriptor = descriptor_;
				lease_->baseline = baseline_;
				lease_->remaining = hold_;
				context.world.GetStorage().Get<TimeLeaseStorage>().values.push_back(lease_);
			}
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (context.isScrubbing) {
				if (lease_) lease_->active = false;
				if (!hitStop_ && !global_ && OwnsTimelineControl(context.world, context.target,
					descriptor_.componentName, descriptor_.propertyPath, context.control))
					QueueTimelinePropertySample(context, descriptor_, baseline_, scale_);
				else if (CanRestoreTimelineControl(context.world, context.target, descriptor_.componentName, descriptor_.propertyPath, context.control))
					descriptor_.setValue(context.world, context.target, baseline_);
				return;
			}
			if (context.mode == TimelineEvaluationMode::Preview || !OwnsTimelineControl(context.world, context.target,
				descriptor_.componentName, descriptor_.propertyPath, context.control)) return;
			if (hitStop_) {
				if (lease_ && !lease_->expired) descriptor_.setValue(context.world, context.target, 0.0f);
			} else QueueTimelinePropertySample(context, descriptor_, baseline_, scale_);
		}

		void Retire([[maybe_unused]] TimelineEvaluationContext& context) override {

			if (lease_) lease_->active = false;
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			if (lease_) lease_->active = false;
			RestoreTimelinePropertyBase(context, descriptor_, true);
		}

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip,
			[[maybe_unused]] std::string& diagnostic) override {

			TimeClip candidate(clip);
			if (global_ != candidate.global_) return false;
			scale_ = candidate.scale_;
			hold_ = candidate.hold_;
			hitStop_ = candidate.hitStop_;
			if (lease_) {
				lease_->remaining = (std::min)(lease_->remaining, hold_);
				if (!hitStop_) {
					lease_->active = false;
					lease_.reset();
					descriptor_.setValue(context.world, context.target, baseline_);
				}
			}
			return true;
		}
	private:

		float scale_ = 0.25f, hold_ = 0.1f;
		bool global_ = false, hitStop_ = false;
		AnimationPropertyDescriptor descriptor_;
		AnimationPropertyValue baseline_{};
		std::shared_ptr<TimeLease> lease_;
	};
}

void Engine::UpdateBuiltinTimelineRealtime(ECSWorld& world, float unscaledDeltaTime) {

	auto* storage = world.GetStorage().TryGet<TimeLeaseStorage>();
	if (!storage) return;
	const float delta = std::isfinite(unscaledDeltaTime) ? (std::max)(0.0f, unscaledDeltaTime) : 0;
	for (const auto& lease : storage->values) {
		if (!lease->active || lease->expired) continue;
		lease->remaining -= delta;
		if (lease->remaining > 0 && world.IsAlive(lease->target)) {
			if (OwnsTimelineControl(world, lease->target, lease->descriptor.componentName, lease->descriptor.propertyPath, lease->owner))
				lease->descriptor.setValue(world, lease->target, 0.0f);
			continue;
		}
		lease->expired = true;
		const bool held = std::any_of(storage->values.begin(), storage->values.end(), [&](const auto& other) {
			return other->active && !other->expired && other->remaining > 0 && world.IsAlive(other->target) &&
				(other->target == lease->target || lease->descriptor.componentName == "GlobalTime") &&
				other->descriptor.componentName == lease->descriptor.componentName &&
				OwnsTimelineControl(world, other->target, other->descriptor.componentName, other->descriptor.propertyPath, other->owner);
		});
		if (held) continue;
		if ((lease->descriptor.componentName == "GlobalTime" || world.IsAlive(lease->target)) &&
			OwnsTimelineControl(world, lease->target, lease->descriptor.componentName,
			lease->descriptor.propertyPath, lease->owner)) lease->descriptor.setValue(world, lease->target, lease->baseline);
	}
	std::erase_if(storage->values, [](const auto& lease) { return !lease->active; });
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineTimeClip(const TimelineClip& clip) {

	return std::make_unique<TimeClip>(clip);
}
