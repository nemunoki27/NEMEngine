#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	// 2Dと3Dの共通出力設定を取得する
	CameraCommon* Camera(ECSWorld& world, Entity entity) {

		if (auto* camera = world.TryGetComponentForBinding<PerspectiveCameraComponent>(entity)) return &camera->common;
		if (auto* camera = world.TryGetComponentForBinding<OrthographicCameraComponent>(entity)) return &camera->common;
		return nullptr;
	}

	// 出力設定の復元も数値プロパティと同じ所有経路に揃える
	AnimationPropertyDescriptor CommonDescriptor(int field) {

		AnimationPropertyDescriptor result;
		result.componentName = "TimelineCamera";
		result.propertyPath = field == 0 ? "enabled" : field == 1 ? "isMain" : "priority";
		result.valueType = AnimationValueType::Int;
		result.getValue = [field](ECSWorld& world, const Entity& entity, AnimationPropertyValue& value) {
			const auto* camera = Camera(world, entity); if (!camera) return false;
			value = field == 0 ? int32_t(camera->enabled) : field == 1 ? int32_t(camera->isMain) : camera->priority;
			return true;
		};
		result.setValue = [field](ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) {
			auto* camera = Camera(world, entity); const auto* number = std::get_if<int32_t>(&value);
			if (!camera || !number) return false;
			if (field == 0) camera->enabled = *number != 0; else if (field == 1) camera->isMain = *number != 0; else camera->priority = *number;
			world.MarkRenderDataModified(entity); return true;
		};
		return result;
	}

	struct CameraProperty {

		AnimationPropertyDescriptor descriptor;
		AnimationPropertyValue base;
	};

	class CameraClip final : public ITimelineClipInstance {
	public:

		explicit CameraClip(const TimelineClip& clip) : shot_(clip.data.value("shotBinding", "")),
			fov_(clip.data.value("fov", 60.0f)), frequency_(clip.data.value("shakeFrequency", 12.0f)), seed_(clip.data.value("seed", 1u)) {

			if (clip.data.contains("shake")) shake_ = Vector3::FromJson(clip.data.at("shake"));
			if (!std::isfinite(fov_) || fov_ < 1 || fov_ > 179 || !std::isfinite(frequency_) || frequency_ < 0 ||
				!std::isfinite(shake_.x) || !std::isfinite(shake_.y) || !std::isfinite(shake_.z))
				throw std::invalid_argument("カメラの画角・シェイク設定が不正です");
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (!Camera(context.world, context.target) || !TransformWorldUtility::ResolveWorldTransform(context.world, context.target, origin_, true)) {
				diagnostic = "カメラとTransformを持つ対象を割り当ててください"; return false;
			}
			auto& registry = AnimationPropertyRegistry::GetInstance();
			for (const auto& [name, type] : {std::pair{"localPos", AnimationValueType::Vector3}, {"localRotation", AnimationValueType::Quaternion}}) {
				const auto descriptor = registry.ResolveProperty(context.world, context.target, "Transform", name, type);
				if (descriptor) Capture(context, *descriptor);
			}
			if (const auto descriptor = registry.ResolveProperty(context.world, context.target, "Transform", "localScale", AnimationValueType::Vector3))
				Capture(context, *descriptor);
			if (const auto descriptor = registry.ResolveProperty(context.world, context.target, "PerspectiveCamera", "fovY", AnimationValueType::Float))
				Capture(context, *descriptor);
			for (int i = 0; i < 3; ++i) Capture(context, CommonDescriptor(i));
			AcquireTimelineControl(context.world, context.target, "Camera", {}, context.control);
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (!OwnsTimelineControl(context.world, context.target, "Camera", {}, context.control)) return;
			ResolvedWorldTransform pose = origin_, parent;
			float fov = fov_;
			const auto shot = context.ResolveBinding(shot_);
			if (!shot_.empty() && TransformWorldUtility::ResolveWorldTransform(context.world, shot, pose, true)) {
				if (const auto* camera = context.world.TryGetComponentForBinding<PerspectiveCameraComponent>(shot)) fov = camera->fovY;
			}
			if (!TransformWorldUtility::ResolveParentFollowTransform(context.world, context.target, parent, true)) return;
			Matrix4x4 inverse;
			if (!Matrix4x4::TryInverse(parent.matrix, inverse)) return;
			const float phase = static_cast<float>(context.localTime) * frequency_;
			const float seed = static_cast<float>(seed_ % 997);
			const Vector3 offset(std::sin(phase * 6.2831853f + seed) * shake_.x,
				std::sin(phase * 7.137f + seed * 1.3f) * shake_.y, std::sin(phase * 5.527f + seed * 2.1f) * shake_.z);
			for (const auto& state : properties_) {
				if (!OwnsTimelineControl(context.world, context.target, state.descriptor.componentName,
					state.descriptor.propertyPath, context.control)) continue;
				AnimationPropertyValue value;
				if (state.descriptor.componentName == "Transform") {
					if (state.descriptor.propertyPath == "localPos") value = Vector3::Transform(pose.matrix.GetTranslationValue() + offset, inverse);
					else if (state.descriptor.propertyPath == "localRotation") value = (Quaternion::Inverse(parent.rotation) * pose.rotation).Normalize();
					else value = Vector3(parent.scale.x == 0 ? 1 : pose.scale.x / parent.scale.x,
						parent.scale.y == 0 ? 1 : pose.scale.y / parent.scale.y, parent.scale.z == 0 ? 1 : pose.scale.z / parent.scale.z);
				} else if (state.descriptor.propertyPath == "fovY") value = std::clamp(fov, 1.0f, 179.0f);
				else value = state.descriptor.propertyPath == "priority" ? int32_t(1000000) : int32_t(1);
				const float weight = context.weight;
				if (state.descriptor.componentName == "TimelineCamera") context.weight = 1;
				QueueTimelinePropertySample(context, state.descriptor, state.base, value);
				context.weight = weight;
			}
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			for (const auto& state : properties_) RestoreTimelinePropertyBase(context, state.descriptor, true);
			properties_.clear();
		}

		bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) override {

			CameraClip candidate(clip);
			shot_ = std::move(candidate.shot_);
			fov_ = candidate.fov_;
			frequency_ = candidate.frequency_;
			seed_ = candidate.seed_;
			shake_ = candidate.shake_;
			return true;
		}
	private:

		std::string shot_;
		float fov_ = 60, frequency_ = 12;
		uint32_t seed_ = 1;
		Vector3 shake_{};
		ResolvedWorldTransform origin_;
		std::vector<CameraProperty> properties_;

		// ブレンド先の通常値を一度だけ保存する
		void Capture(TimelineEvaluationContext& context, const AnimationPropertyDescriptor& descriptor) {

			AcquireTimelineControl(context.world, context.target, descriptor.componentName, descriptor.propertyPath, context.control);
			properties_.push_back({descriptor, CaptureTimelinePropertyBase(context, descriptor)});
		}
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineCameraClip(const TimelineClip& clip) {

	return std::make_unique<CameraClip>(clip);
}
