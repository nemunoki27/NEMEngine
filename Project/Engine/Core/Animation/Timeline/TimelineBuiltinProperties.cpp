#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Animation/AnimationEventCollection.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <optional>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	// 登録されたプロパティ型と同じ値へ変換する
	AnimationPropertyValue ReadValue(AnimationValueType type, const nlohmann::json& value) {

		switch (type) {
		case AnimationValueType::Float: return value.get<float>();
		case AnimationValueType::Int: return value.get<int32_t>();
		case AnimationValueType::Vector2: return Vector2::FromJson(value);
		case AnimationValueType::Vector3: return Vector3::FromJson(value);
		case AnimationValueType::Vector4: return Vector4::FromJson(value);
		case AnimationValueType::Color3: return Color3::FromJson(value);
		case AnimationValueType::Color4: return Color4::FromJson(value);
		case AnimationValueType::Quaternion: return Quaternion::FromJson(value);
		}
		throw std::invalid_argument("対応していないプロパティ型です");
	}

	struct PropertyState {

		AnimationPropertyDescriptor descriptor;
		AnimationPropertyValue base{};
		bool restore = true;
		bool captured = false;
	};

	class PropertyClip final : public ITimelineClipInstance {
	public:

		explicit PropertyClip(const TimelineClip& clip) : asset_(ParseAssetID(clip.data, "asset")),
			restore_(clip.data.value("restoreOnEnd", true)), easing_(clip.easing) {

			if (asset_) return;
			binding_ = clip.data.at("binding").get<AnimationPropertyBinding>();
			from_ = ReadValue(binding_.valueType, clip.data.at("from"));
			to_ = ReadValue(binding_.valueType, clip.data.at("to"));
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			previousTime_ = context.localTime;
			eventStart_ = true;
			if (asset_) return RefreshSource(context, diagnostic);
			std::vector<AnimationPreviewBaseValue> values;
			AnimationPropertySnapshot::Capture(context.world, context.target, binding_, values);
			return BindProperties(context, std::move(values), diagnostic);
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			std::string diagnostic;
			if (asset_ && !RefreshSource(context, diagnostic)) throw std::invalid_argument(diagnostic);
			if (clip_) {
				AnimationClipEvaluator::EvaluateClipValues(context.world, context.target, *clip_,
					AnimationClipEvaluator::ResolveClipEvaluationTime(*clip_, static_cast<float>(context.localTime)), baseValues_, values_);
				if (clip_->relativeTransform) AnimationClipEvaluator::ComposeRelativeTransform(values_, baseValues_, neutral_);
				else AnimationClipEvaluator::PreserveUnkeyedChannels(context.world, context.target, *clip_, values_);
			} else {
				AnimationPropertyValue result;
				const float phase = static_cast<float>(std::clamp(context.localTime /
					(std::max)(GetTimelineClipSourceDuration(context.clip), 0.0001), 0.0, 1.0));
				AnimationValueOperations::LerpValue(from_, to_, EasedValue(easing_, phase), result);
				values_.assign(1, {binding_, result});
			}
			for (const auto& value : values_) {
				for (const auto& state : states_) {
					if (value.binding.componentName != state.descriptor.componentName ||
						value.binding.propertyPath != state.descriptor.propertyPath ||
						!OwnsTimelineControl(context.world, context.target, value.binding.componentName, value.binding.propertyPath, context.control)) continue;
					QueueTimelinePropertySample(context, state.descriptor, state.base, value.value);
				}
			}
			DispatchEvents(context);
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			for (const auto& state : states_) {
				if (state.captured) RestoreTimelinePropertyBase(context, state.descriptor,
					state.restore && (restore_ || context.mode == TimelineEvaluationMode::Preview));
			}
			states_.clear();
			baseValues_.clear();
			neutral_.clear();
		}

		bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) override {

			PropertyClip candidate(clip);
			if (asset_ != candidate.asset_ || (!asset_ && !AnimationValueOperations::SameBinding(binding_, candidate.binding_)))
				return false;
			restore_ = candidate.restore_;
			easing_ = candidate.easing_;
			from_ = candidate.from_;
			to_ = candidate.to_;
			return true;
		}
	private:

		AssetID asset_{};
		bool restore_ = true;
		EasingType easing_{};
		AnimationPropertyBinding binding_;
		AnimationPropertyValue from_{}, to_{};
		std::optional<AnimationClipAsset> clip_;
		std::vector<PropertyState> states_;
		std::vector<AnimationPreviewBaseValue> baseValues_;
		std::vector<AnimationEvaluatedValue> values_;
		std::vector<AnimationEvaluatedValue> neutral_;
		double previousTime_ = 0;
		bool eventStart_ = true;
		bool sourceLoaded_ = false, sourceOverride_ = false;
		uint64_t sourceRevision_ = 0;

		// 既存プロパティの基準値を保って追加と削除だけを反映する
		bool BindProperties(TimelineEvaluationContext& context, std::vector<AnimationPreviewBaseValue> values,
			std::string& diagnostic) {

			if (values.empty() && !asset_) { diagnostic = "対象にアニメーション可能なプロパティがありません"; return false; }
			std::vector<PropertyState> next;
			next.reserve(values.size());
			for (const auto& base : values) {
				const auto& binding = base.binding;
				const auto descriptor = AnimationPropertyRegistry::GetInstance().ResolveProperty(context.world,
					context.target, binding.componentName, binding.propertyPath, binding.valueType);
				if (!descriptor || !descriptor->getValue || !descriptor->setValue) {
					diagnostic = "対象にアニメーション可能なプロパティがありません";
					return false;
				}
				const auto previous = std::find_if(states_.begin(), states_.end(), [&](const auto& state) {
					return state.descriptor.componentName == binding.componentName && state.descriptor.propertyPath == binding.propertyPath &&
						state.descriptor.valueType == binding.valueType;
				});
				next.push_back(previous == states_.end() ? PropertyState{*descriptor, base.value, base.restore} : *previous);
			}
			for (size_t i = 0; i < next.size(); ++i) {
				auto& state = next[i];
				if (!state.captured) {
					AcquireTimelineControl(context.world, context.target, state.descriptor.componentName,
						state.descriptor.propertyPath, context.control);
					state.base = CaptureTimelinePropertyBase(context, state.descriptor);
					state.captured = true;
				}
				values[i].value = state.base;
			}
			for (const auto& state : states_) {
				if (std::any_of(next.begin(), next.end(), [&](const auto& value) {
					return value.descriptor.componentName == state.descriptor.componentName &&
						value.descriptor.propertyPath == state.descriptor.propertyPath && value.descriptor.valueType == state.descriptor.valueType;
				})) continue;
				RestoreTimelinePropertyBase(context, state.descriptor, true);
				ReleaseTimelineControl(context.world, context.target, state.descriptor.componentName,
					state.descriptor.propertyPath, context.control);
			}
			states_ = std::move(next);
			baseValues_ = std::move(values);
			return true;
		}

		// 保存前の編集世代だけを取り込み同時刻のイベントを再送しない
		bool RefreshSource(TimelineEvaluationContext& context, std::string& diagnostic) {

			const auto draft = GetAnimationClipAssetOverride(context.world, asset_);
			const AnimationClipAsset* source = draft.get();
			uint64_t revision = GetAnimationClipAssetOverrideRevision(context.world, asset_);
			if (!source && context.systems.animationClipManager && context.systems.assetDatabase) {
				source = context.systems.animationClipManager->GetOrLoad(*context.systems.assetDatabase, asset_);
				revision = context.systems.animationClipManager->GetRevision(asset_);
			}
			if (!source) { diagnostic = "アニメーションクリップを読み込めません"; return false; }
			if (sourceLoaded_ && sourceOverride_ == (draft != nullptr) && sourceRevision_ == revision) return true;
			std::vector<AnimationPreviewBaseValue> values;
			AnimationPropertySnapshot::CaptureClip(context.world, context.target, *source, false, values);
			for (const auto& track : source->curveTracks) {
				if (AnimationPropertySnapshot::Contains(values, track.binding)) continue;
				diagnostic = "対象にクリップのプロパティがありません: " + track.binding.componentName + "." + track.binding.propertyPath;
				return false;
			}
			const auto previous = baseValues_;
			if (!BindProperties(context, std::move(values), diagnostic)) return false;
			clip_ = *source;
			neutral_.clear();
			if (clip_->relativeTransform) {
				AnimationPropertySnapshot::CaptureRelativeTransform(context.world, context.target, baseValues_);
				for (auto& base : baseValues_) {
					const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& value) {
						return AnimationValueOperations::SameBinding(base.binding, value.binding);
					});
					if (old != previous.end()) base.value = old->value;
				}
				AnimationClipEvaluator::EvaluateClipValues(context.world, context.target,
					*clip_, AnimationResolvedTime{}, baseValues_, neutral_);
			}
			if (sourceLoaded_) { previousTime_ = context.localTime; eventStart_ = false; }
			sourceLoaded_ = true;
			sourceOverride_ = draft != nullptr;
			sourceRevision_ = revision;
			return true;
		}

		// 実際に通過した区間だけを既存のAnimationEvent配送へ渡す
		void DispatchEvents(TimelineEvaluationContext& context) {

			const double previous = previousTime_;
			previousTime_ = context.localTime;
			if (!clip_ || clip_->events.empty()) return;
			if (context.isScrubbing || context.mode != TimelineEvaluationMode::Playback) { eventStart_ = false; return; }
			if (context.paused || context.localTime < previous) return;
			std::vector<AnimationPlaybackTime::Interval> intervals;
			const double duration = (std::max)(0.001, static_cast<double>(clip_->duration));
			if (!clip_->loop) intervals.push_back({static_cast<float>((std::min)(previous, duration)),
				static_cast<float>((std::min)(context.localTime, duration)), eventStart_});
			else {
				const double period = AnimationClipEvaluator::GetPlaybackDuration(*clip_);
				const double first = std::floor(previous / period), last = std::floor(context.localTime / period);
				if (last - first > 1024) throw std::invalid_argument("アニメーションイベントの周回数が多すぎます");
				const auto count = static_cast<uint32_t>(last - first) + 1;
				for (uint32_t i = 0; i < count; ++i) {
					const double cycle = first + i;
					const double from = cycle == first ? previous - cycle * period : 0;
					const double to = cycle == last ? context.localTime - cycle * period : period;
					if (from > duration) continue;
					intervals.push_back({static_cast<float>(from), static_cast<float>((std::min)(to, duration)), eventStart_ || cycle > first});
				}
			}
			eventStart_ = false;
			std::vector<AnimationEvent> events;
			AnimationEventCollection::CollectClipEvents(*clip_, intervals, events);
			for (const auto& event : events) {
				if (context.systems.IsUpdateInterrupted() || !context.world.IsAlive(context.target)) break;
				BehaviorSystem::DispatchAnimationEvent(context.world, context.systems, context.target,
					event.name, event.floatParam, event.intParam, event.stringParam);
			}
		}
	};

	class ToggleClip final : public ITimelineClipInstance {
	public:

		ToggleClip(TimelineTrackKind kind, const TimelineClip& clip) : collision_(kind == TimelineTrackKind::Collision),
			active_(clip.data.value("active", true)) {

			descriptor_.componentName = collision_ ? "Collision" : "SceneObject";
			descriptor_.propertyPath = collision_ ? "enabled" : "activeSelf";
			descriptor_.valueType = AnimationValueType::Int;
			descriptor_.getValue = [collision = collision_](ECSWorld& world, const Entity& entity, AnimationPropertyValue& value) {
				if (collision) {
					const auto* component = world.TryGetComponentForBinding<CollisionComponent>(entity);
					if (!component) return false;
					value = int32_t(component->enabled);
				} else {
					const auto* component = world.TryGetComponentForBinding<SceneObjectComponent>(entity);
					if (!component) return false;
					value = int32_t(component->activeSelf);
				}
				return true;
			};
			descriptor_.setValue = [collision = collision_](ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) {
				const auto* number = std::get_if<int32_t>(&value);
				if (!number) return false;
				if (collision) {
					auto* component = world.TryGetComponentForBinding<CollisionComponent>(entity);
					if (!component) return false;
					component->enabled = *number != 0;
					world.MarkComponentModified<CollisionComponent>(entity);
				} else {
					auto* component = world.TryGetComponentForBinding<SceneObjectComponent>(entity);
					if (!component) return false;
					component->activeSelf = *number != 0;
					HierarchySystem{}.UpdateActiveInHierarchy(world, entity);
					world.MarkComponentModified<SceneObjectComponent>(entity);
				}
				return true;
			};
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (collision_) {
				const auto* collider = context.world.TryGetComponentForBinding<CollisionComponent>(context.target);
				if (!collider) { diagnostic = "対象にCollisionを追加してください"; return false; }
			} else {
				const auto* scene = context.world.TryGetComponentForBinding<SceneObjectComponent>(context.target);
				if (!scene) { diagnostic = "有効状態を変更する対象がありません"; return false; }
				if (context.target == context.owner && !active_) { diagnostic = "再生元自身を無効にはできません"; return false; }
			}
			AcquireTimelineControl(context.world, context.target, descriptor_.componentName, descriptor_.propertyPath, context.control);
			CaptureTimelinePropertyBase(context, descriptor_);
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (OwnsTimelineControl(context.world, context.target, descriptor_.componentName, descriptor_.propertyPath, context.control))
				descriptor_.setValue(context.world, context.target, int32_t(collision_ || active_));
		}

		bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) override {

			active_ = clip.data.value("active", true);
			return true;
		}

		void Exit(TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			const bool restore = !collision_ || context.mode == TimelineEvaluationMode::Preview;
			RestoreTimelinePropertyBase(context, descriptor_, restore);
			if (!restore && CanRestoreTimelineControl(context.world, context.target, descriptor_.componentName, descriptor_.propertyPath, context.control))
				descriptor_.setValue(context.world, context.target, int32_t(0));
		}
	private:

		bool collision_ = false, active_ = true;
		AnimationPropertyDescriptor descriptor_;
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelinePropertyClip(TimelineTrackKind kind, const TimelineClip& clip) {

	if (kind == TimelineTrackKind::Property) return std::make_unique<PropertyClip>(clip);
	return std::make_unique<ToggleClip>(kind, clip);
}
