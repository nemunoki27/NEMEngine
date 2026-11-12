#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <optional>
#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	class SkinnedClip final : public ITimelineClipInstance {
	public:

		explicit SkinnedClip(const TimelineClip& clip) : name_(clip.data.value("clip", "")), loop_(clip.data.value("loop", false)) {}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (!context.world.TryGetComponentForBinding<SkinnedAnimationComponent>(context.target)) {
				diagnostic = "対象にSkinnedAnimationを追加してください"; return false;
			}
			AcquireTimelineControl(context.world, context.target, "SkinnedAnimation", {}, context.control);
			if (OwnsTimelineControl(context.world, context.target, "SkinnedAnimation", {}, context.control)) {
				if (auto* runtime = TryGetSkinnedAnimationRuntime(context.world, context.target)) runtime->timelineControlled = true;
			}
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (!OwnsTimelineControl(context.world, context.target, "SkinnedAnimation", {}, context.control)) return;
			bool changed = false;
			const auto definition = PrepareSkinnedAnimation(context.world, context.target, context.systems, changed);
			ready_ = definition != nullptr;
			if (!ready_) {
				const auto* mesh = context.world.TryGetComponentForBinding<MeshRendererComponent>(context.target);
				if (!mesh || !mesh->mesh) throw std::invalid_argument("対象に骨アニメーション付きMeshを設定してください");
				if (context.systems.skinnedAnimationManager) {
					if (context.systems.skinnedAnimationManager->HasLoadFailed(mesh->mesh))
						throw std::invalid_argument("骨アニメーションを読み込めません。モデルのパス・骨格・クリップを確認してください");
				}
				return;
			}
			if (!name_.empty() && !definition->clips.contains(name_))
				throw std::invalid_argument("指定した骨アニメーションクリップがありません: " + name_);
			QueueTimelineSkinnedSample(context, {name_, {}, static_cast<float>(context.localTime), 0, 1, loop_});
		}

		float GetPreparationProgress() const override { return ready_ ? 1.0f : 0.0f; }

		bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) override {

			name_ = clip.data.value("clip", "");
			loop_ = clip.data.value("loop", false);
			return true;
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			if (!CanRestoreTimelineControl(context.world, context.target, "SkinnedAnimation", {}, context.control)) return;
			if (auto* runtime = TryGetSkinnedAnimationRuntime(context.world, context.target)) {
				const std::string name = runtime->currentClip;
				SampleSkinnedAnimation(context.world, context.target, context.systems, {name, {}, runtime->time});
				runtime->timelineControlled = false;
				runtime->timelineClip.clear();
			}
		}
	private:

		std::string name_;
		bool loop_ = false, ready_ = false;
	};

	class MotionClip final : public ITimelineClipInstance {
	public:

		explicit MotionClip(const TimelineClip& clip) : target_(clip.data.value("targetBinding", "")),
			spline_(clip.data.value("splineBinding", "")) {

			if (clip.data.contains("settings")) settings_ = clip.data.at("settings").get<MotionAnimationSettings>();
			settings_.repeat = SimpleAnimationRepeat::Once;
			settings_.delay = 0;
			settings_.speed = 1;
			settings_.easing = clip.easing;
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			ResolvedWorldTransform pose;
			if (!TransformWorldUtility::ResolveWorldTransform(context.world, context.target, pose, true)) {
				diagnostic = "移動対象のTransformがありません"; return false;
			}
			if (!originCaptured_ || !context.isScrubbing) {
				runtime_.origin = pose.matrix.GetTranslationValue();
				runtime_.originRotation = pose.rotation;
				originCaptured_ = true;
			}
			if (!CaptureTarget(context, diagnostic)) return false;
			if (settings_.mode == MotionAnimationMode::Spline) {
				const auto entity = spline_.empty() ? settings_.spline.Resolve(context.world, context.target) : context.ResolveBinding(spline_);
				const auto* spline = TryGetSpline(context.world, entity);
				if (!spline || !TransformWorldUtility::ResolveWorldTransform(context.world, entity, pose, true)) {
					diagnostic = "Splineを持つ対象を割り当ててください"; return false;
				}
				runtime_.splineSamples = BuildSplineSamples(*spline, pose.matrix);
			}
			auto& registry = AnimationPropertyRegistry::GetInstance();
			if (settings_.applyPosition) position_ = registry.ResolveProperty(context.world, context.target,
				"Transform", "localPos", AnimationValueType::Vector3);
			if (settings_.applyRotation) rotation_ = registry.ResolveProperty(context.world, context.target,
				"Transform", "localRotation", AnimationValueType::Quaternion);
			if (position_) Capture(context, *position_, positionBase_);
			if (rotation_) Capture(context, *rotation_, rotationBase_);
			BuildDebugPath();
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (settings_.trackTarget) {
				const auto previous = runtime_.targetPosition;
				std::string diagnostic;
				if (!CaptureTarget(context, diagnostic)) throw std::invalid_argument(diagnostic);
				if (previous != runtime_.targetPosition) BuildDebugPath();
			}
			const auto sample = EvaluateMotionAnimation(settings_, runtime_,
				GetMotionAnimationDuration(settings_) * std::clamp(context.localTime /
					(std::max)(GetTimelineClipSourceDuration(context.clip), 0.0001), 0.0, 1.0));
			ResolvedWorldTransform parent;
			if (!TransformWorldUtility::ResolveParentFollowTransform(context.world, context.target, parent, true)) return;
			Matrix4x4 inverse;
			if (!Matrix4x4::TryInverse(parent.matrix, inverse)) return;
			if (position_ && Owns(context, *position_)) QueueTimelinePropertySample(context, *position_, positionBase_,
				Vector3::Transform(sample.position, inverse));
			if (rotation_ && Owns(context, *rotation_)) QueueTimelinePropertySample(context, *rotation_, rotationBase_,
				(Quaternion::Inverse(parent.rotation) * sample.rotation).Normalize());
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			const bool restore = (context.mode == TimelineEvaluationMode::Preview || context.isScrubbing) && reason != TimelineExitReason::Completed;
			if (position_) RestoreTimelinePropertyBase(context, *position_, restore);
			if (rotation_) RestoreTimelinePropertyBase(context, *rotation_, restore);
		}

		std::span<const Vector3> GetDebugPath() const override { return debugPath_; }

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip,
			std::string& diagnostic) override {

			MotionClip candidate(clip);
			if (settings_.applyPosition != candidate.settings_.applyPosition || settings_.applyRotation != candidate.settings_.applyRotation) {
				if (position_) RestoreTimelinePropertyBase(context, *position_, true);
				if (rotation_) RestoreTimelinePropertyBase(context, *rotation_, true);
				ReleaseTimelineControls(context.world, context.control);
				position_.reset();
				rotation_.reset();
				auto& registry = AnimationPropertyRegistry::GetInstance();
				if (candidate.settings_.applyPosition) position_ = registry.ResolveProperty(context.world, context.target,
					"Transform", "localPos", AnimationValueType::Vector3);
				if (candidate.settings_.applyRotation) rotation_ = registry.ResolveProperty(context.world, context.target,
					"Transform", "localRotation", AnimationValueType::Quaternion);
				if (position_) Capture(context, *position_, positionBase_);
				if (rotation_) Capture(context, *rotation_, rotationBase_);
			}
			settings_ = candidate.settings_;
			target_ = std::move(candidate.target_);
			spline_ = std::move(candidate.spline_);
			if (!CaptureTarget(context, diagnostic)) return false;
			if (settings_.mode == MotionAnimationMode::Spline) {
				const auto entity = spline_.empty() ? settings_.spline.Resolve(context.world, context.target) : context.ResolveBinding(spline_);
				const auto* spline = TryGetSpline(context.world, entity);
				ResolvedWorldTransform pose;
				if (!spline || !TransformWorldUtility::ResolveWorldTransform(context.world, entity, pose, true)) {
					diagnostic = "Splineを持つ対象を割り当ててください";
					return false;
				}
				runtime_.splineSamples = BuildSplineSamples(*spline, pose.matrix);
			}
			BuildDebugPath();
			return true;
		}
	private:

		MotionAnimationSettings settings_;
		MotionAnimationRuntime runtime_;
		std::string target_, spline_;
		std::optional<AnimationPropertyDescriptor> position_, rotation_;
		AnimationPropertyValue positionBase_{}, rotationBase_{};
		std::vector<Vector3> debugPath_;
		bool originCaptured_ = false;

		// 経路が変わった時だけ補助線の評価点を作り直す
		void BuildDebugPath() {

			debugPath_.clear();
			if (!settings_.drawDebug) return;
			debugPath_.reserve(33);
			for (int i = 0; i <= 32; ++i) debugPath_.push_back(EvaluateMotionAnimation(settings_, runtime_,
				GetMotionAnimationDuration(settings_) * i / 32.0).position);
		}

		// 開始時に固定する対象と追尾対象を同じ経路で取得する
		bool CaptureTarget(TimelineEvaluationContext& context, std::string& diagnostic) {

			if (settings_.mode != MotionAnimationMode::ApproachTarget && settings_.rotation != MotionAnimationRotation::LookAtTarget) return true;
			const auto entity = target_.empty() ? settings_.target.Resolve(context.world, context.target) : context.ResolveBinding(target_);
			ResolvedWorldTransform pose;
			if (!TransformWorldUtility::ResolveWorldTransform(context.world, entity, pose, true)) {
				diagnostic = "移動・注視先の対象がありません"; return false;
			}
			runtime_.targetPosition = pose.matrix.GetTranslationValue();
			return true;
		}
		// 対象プロパティの制御権と共有復元値を取得する
		void Capture(TimelineEvaluationContext& context, const AnimationPropertyDescriptor& descriptor, AnimationPropertyValue& value) {

			AcquireTimelineControl(context.world, context.target, descriptor.componentName, descriptor.propertyPath, context.control);
			value = CaptureTimelinePropertyBase(context, descriptor);
		}
		// 現在のPlayerが書き込み先を所有しているか調べる
		bool Owns(TimelineEvaluationContext& context, const AnimationPropertyDescriptor& descriptor) {

			return OwnsTimelineControl(context.world, context.target, descriptor.componentName, descriptor.propertyPath, context.control);
		}
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineAnimationClip(TimelineTrackKind kind, const TimelineClip& clip) {

	if (kind == TimelineTrackKind::SkinnedAnimation) return std::make_unique<SkinnedClip>(clip);
	return std::make_unique<MotionClip>(clip);
}
