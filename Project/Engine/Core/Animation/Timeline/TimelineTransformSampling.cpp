#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlayerAccess.h"
#include "TimelineBuiltinConfig.h"
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSampling.h>
#include <Engine/Core/World/Systems/Animation/SkeletonPoseEvaluator.h>

// c++
#include <algorithm>
#include <cmath>
#include <bit>

using namespace Engine;

namespace {

	// 再生側の更新順に依存せず現在の割り当てを参照する
	Entity ResolveTarget(const TimelinePlaybackState& state, const TimelineClipPlayback& playback) {

		for (const auto& binding : state.bindings) if (binding.id == playback.track->binding) return binding.entity;
		return Entity::Null();
	}

	struct SamplingFrame {

		Entity entity{};
		double time = 0;
		size_t limit = 0;
	};
	struct CachedTransform {

		SamplingFrame frame;
		Matrix4x4 matrix;
	};
	struct TransformSampler {

		ECSWorld& world;
		Entity owner;
		TimelinePlayerRecord& player;
		TimelinePlaybackState& state;
		SystemContext& systems;
		std::vector<SamplingFrame> stack;
		std::vector<CachedTransform> cache;

		// 子の時刻を親クリップの配置時刻へ戻す
		double ParentTime(double time) const {

			if (state.samplingSpeed <= 0) return state.samplingParentStart;
			return std::clamp(state.samplingParentStart + (time + state.samplingCycle - state.samplingClipIn) / state.samplingSpeed,
				state.samplingParentStart, state.samplingParentEnd);
		}

		// 親の姿勢をそのまま使える対象と子が変更する対象を分ける
		bool HasInputs(Entity target) {

			std::vector<Entity> entities{target};
			for (size_t i = 0; i < entities.size(); ++i) {
				const auto entity = entities[i];
				if (GetTimelineTransformChannels(world, state, entity, systems)) return true;
				Entity parent{};
				if (const auto* attachment = world.TryGetComponentForBinding<JointAttachmentComponent>(entity);
					attachment && !attachment->jointName.empty() && attachment->skinnedEntityLocalFileID) {
					parent = SceneObjectUtility::FindByLocalFileID(world, SceneObjectUtility::GetSceneInstanceID(world, entity), attachment->skinnedEntityLocalFileID);
					if (HasSkinned(parent)) return true;
				} else if (const auto* hierarchy = world.TryGetComponentForBinding<HierarchyComponent>(entity)) parent = hierarchy->parent;
				if (world.IsAlive(parent) && std::find(entities.begin(), entities.end(), parent) == entities.end()) entities.push_back(parent);
			}
			return false;
		}

		// 骨格を子側で評価する必要があるか調べる
		bool HasSkinned(Entity entity) const {

			return std::any_of(state.clips.begin(), state.clips.end(), [&](const auto& clip) {
				return ResolveTarget(state, clip) == entity && clip.track->kind == TimelineTrackKind::SkinnedAnimation;
			});
		}

		// 評価対象の成分だけをプレビュー開始時の値へ揃える
		TransformComponent Baseline(Entity entity, const TransformComponent& transform) {

			TransformComponent local = transform;
			const auto channels = GetTimelineTransformChannels(world, state, entity, systems);
			for (const auto& pose : state.previewPoses) {
				if (pose.entity != entity) continue;
				if (channels & 1) local.localPos = pose.position;
				if (channels & 2) local.localRotation = pose.rotation;
				if (channels & 4) local.localScale = pose.scale;
				break;
			}
			return local;
		}

		// 保存された姿勢から過去の親と対象を再帰評価する
		bool Resolve(Entity entity, double time, size_t limit, Matrix4x4& result) {

			if (stack.size() >= 64 || !world.IsAlive(entity)) return false;
			for (const auto& entry : cache) {
				if (entry.frame.entity == entity && entry.frame.limit == limit && entry.frame.time == time) {
					result = entry.matrix;
					return true;
				}
			}
			if (state.sampleParentTransform && !HasInputs(entity) && state.sampleParentTransform(ParentTime(time), entity, result)) {
				cache.push_back({{entity, time, limit}, result});
				return true;
			}
			if (std::any_of(stack.begin(), stack.end(), [&](const auto& frame) {
				return frame.entity == entity && frame.limit == limit && frame.time == time;
			})) return false;
			const auto* transform = world.TryGetComponentForBinding<TransformComponent>(entity);
			if (!transform) return false;
			stack.push_back({entity, time, limit});
			ScopedCleanup pop([&]() noexcept { stack.pop_back(); });
			TransformComponent local = Baseline(entity, *transform);
			Matrix4x4 parent = Matrix4x4::Identity();
			Entity parentEntity{};
			bool attached = false;
			if (const auto* attachment = world.TryGetComponentForBinding<JointAttachmentComponent>(entity);
				attachment && !attachment->jointName.empty() && attachment->skinnedEntityLocalFileID) {
				parentEntity = SceneObjectUtility::FindByLocalFileID(world,
					SceneObjectUtility::GetSceneInstanceID(world, entity), attachment->skinnedEntityLocalFileID);
				Matrix4x4 joint, skinned;
				bool resolvedJoint = false;
				if (state.sampleParentTransform && !HasSkinned(parentEntity)) {
					Matrix4x4 outerTarget, outerParent, inverseLocal, inverseParent;
					if (state.sampleParentTransform(ParentTime(time), entity, outerTarget) &&
						state.sampleParentTransform(ParentTime(time), parentEntity, outerParent) &&
						Matrix4x4::TryInverse(MakeLocalMatrix(local), inverseLocal) && Matrix4x4::TryInverse(outerParent, inverseParent)) {
						joint = inverseLocal * outerTarget * inverseParent;
						resolvedJoint = true;
					}
				}
				if (!resolvedJoint) resolvedJoint = Joint(parentEntity, attachment->jointName, time, joint);
				if (Resolve(parentEntity, time, state.clips.size(), skinned) && resolvedJoint) {
					parent = joint * skinned;
					attached = true;
				}
			}
			if (const auto* hierarchy = world.TryGetComponentForBinding<HierarchyComponent>(entity);
				!attached && hierarchy && world.IsAlive(hierarchy->parent)) {
				if (!Resolve(hierarchy->parent, time, state.clips.size(), parent)) return false;
			}
			parent = BuildParentFollowMatrix(parent, local.ignoreParentScale, local.ignoreParentRotation);
			TransformComponent mixed;
			float positionWeight = 0, rotationWeight = 0, scaleWeight = 0;
			const auto blendVector = [](Vector3& accumulated, float& total, const Vector3& value, float weight) {
				accumulated = total <= 0 ? value : Vector3::Lerp(accumulated, value, weight / (total + weight));
				total += weight;
			};
			for (size_t index = 0; index < (std::min)(limit, state.clips.size()); ++index) {
				auto& playback = state.clips[index];
				const auto& clip = *playback.clip;
				if (ResolveTarget(state, playback) != entity || !clip.enabled || time < clip.start ||
					!IsTimelineTrackActive(world, owner, player, state, *playback.track, (std::min)(time, clip.start + clip.duration))) continue;
				const auto kind = playback.track->kind;
				if (kind != TimelineTrackKind::Motion && kind != TimelineTrackKind::Property) continue;
				if (kind == TimelineTrackKind::Property && time > clip.start + clip.duration && clip.data.value("restoreOnEnd", true)) continue;
				const double sampleTime = (std::min)(time, clip.start + clip.duration);
				auto evaluation = MakeTimelineEvaluationContext(world, owner, state, playback, systems, sampleTime, 0);
				evaluation.target = entity;
				const float weight = evaluation.weight;
				if (weight <= 0) continue;
				evaluation.weight = 1;
				auto sample = local;
				const auto affected = kind == TimelineTrackKind::Motion ? Motion(evaluation, index, sample, parent) : Property(evaluation, index, sample);
				if (time > clip.start + clip.duration) { local = sample; continue; }
				if (affected & 1) blendVector(mixed.localPos, positionWeight, sample.localPos, weight);
				if (affected & 4) blendVector(mixed.localScale, scaleWeight, sample.localScale, weight);
				if (affected & 2) {
					mixed.localRotation = rotationWeight <= 0 ? sample.localRotation :
						Quaternion::Lerp(mixed.localRotation, sample.localRotation, weight / (rotationWeight + weight)).Normalize();
					rotationWeight += weight;
				}
			}
			if (positionWeight > 0) local.localPos = Vector3::Lerp(local.localPos, mixed.localPos, (std::min)(positionWeight, 1.0f));
			if (scaleWeight > 0) local.localScale = Vector3::Lerp(local.localScale, mixed.localScale, (std::min)(scaleWeight, 1.0f));
			if (rotationWeight > 0) local.localRotation = Quaternion::Lerp(local.localRotation, mixed.localRotation, (std::min)(rotationWeight, 1.0f)).Normalize();
			result = MakeLocalMatrix(local) * parent;
			cache.push_back({{entity, time, limit}, result});
			return true;
		}

		// 現在の骨格を変更せず指定時刻のジョイントを計算する
		bool Joint(Entity entity, const std::string& name, double time, Matrix4x4& result) {

			bool changed = false;
			const auto definition = PrepareSkinnedAnimation(world, entity, systems, changed);
			const auto* runtime = TryGetSkinnedAnimationRuntime(world, entity);
			if (!definition || !runtime) return false;
			Skeleton pose = runtime->bindSkeleton;
			float weight = 0;
			for (auto& playback : state.clips) {
				const auto& clip = *playback.clip;
				if (ResolveTarget(state, playback) != entity || playback.track->kind != TimelineTrackKind::SkinnedAnimation || !clip.enabled ||
					time < clip.start || time > clip.start + clip.duration ||
					!IsTimelineTrackActive(world, owner, player, state, *playback.track, time)) continue;
				const auto evaluation = MakeTimelineEvaluationContext(world, owner, state, playback, systems, time, 0);
				const auto requested = clip.data.value("clip", std::string{});
				const auto& clipName = requested.empty() ? runtime->currentClip : requested;
				const auto tracks = definition->clipJointTracks.find(clipName);
				const auto source = definition->clips.find(clipName);
				if (tracks == definition->clipJointTracks.end() || source == definition->clips.end() || evaluation.weight <= 0) continue;
				float sourceTime = static_cast<float>(evaluation.localTime);
				if (clip.data.value("loop", false) && source->second.duration > 0) sourceTime = std::fmod(sourceTime, source->second.duration);
				else sourceTime = (std::min)(sourceTime, source->second.duration);
				Skeleton sample = runtime->bindSkeleton;
				SkeletonPoseEvaluator::ApplyClipToSkeleton(sample, tracks->second, sourceTime);
				const float blend = evaluation.weight / (weight + evaluation.weight);
				for (size_t i = 0; i < pose.joints.size(); ++i) {
					auto& value = pose.joints[i].transform;
					const auto& next = sample.joints[i].transform;
					value.translation = Vector3::Lerp(value.translation, next.translation, blend);
					value.scale = Vector3::Lerp(value.scale, next.scale, blend);
					value.rotation = Quaternion::Lerp(value.rotation, next.rotation, blend).Normalize();
				}
				weight += evaluation.weight;
			}
			if (weight < 1) {
				Skeleton underlay = runtime->bindSkeleton;
				if (const auto tracks = definition->clipJointTracks.find(runtime->currentClip); tracks != definition->clipJointTracks.end())
					SkeletonPoseEvaluator::ApplyClipToSkeleton(underlay, tracks->second, runtime->time);
				const float blend = 1 - weight;
				for (size_t i = 0; i < pose.joints.size(); ++i) {
					auto& value = pose.joints[i].transform;
					const auto& next = underlay.joints[i].transform;
					value.translation = Vector3::Lerp(value.translation, next.translation, blend);
					value.scale = Vector3::Lerp(value.scale, next.scale, blend);
					value.rotation = Quaternion::Lerp(value.rotation, next.rotation, blend).Normalize();
				}
			}
			SkeletonPoseEvaluator::UpdateSkeletonHierarchy(pose);
			const int32_t index = FindSkeletonJointIndex(pose, name);
			if (index < 0 || static_cast<size_t>(index) >= pose.joints.size()) return false;
			result = pose.joints[index].skeletonSpaceMatrix;
			return true;
		}

		// 移動原点は対象クリップの直前までを評価して取得する
		uint8_t Motion(TimelineEvaluationContext& context, size_t index, TransformComponent& local, const Matrix4x4& parent) {

			MotionAnimationSettings settings;
			if (context.clip.data.contains("settings")) settings = context.clip.data.at("settings").get<MotionAnimationSettings>();
			settings.repeat = SimpleAnimationRepeat::Once;
			settings.delay = 0;
			settings.speed = 1;
			settings.easing = context.clip.easing;
			MotionAnimationRuntime runtime;
			Matrix4x4 origin;
			if (!Resolve(context.target, context.clip.start, index, origin)) origin = MakeLocalMatrix(local) * parent;
			Vector3 scale;
			DecomposeAffine3D(origin, runtime.origin, runtime.originRotation, scale);
			const std::string targetName = context.clip.data.value("targetBinding", "");
			const auto target = targetName.empty() ? settings.target.Resolve(world, context.target) : context.ResolveBinding(targetName);
			Matrix4x4 targetPose;
			if (Resolve(target, settings.trackTarget ? context.time : context.clip.start, state.clips.size(), targetPose))
				runtime.targetPosition = targetPose.GetTranslationValue();
			if (settings.mode == MotionAnimationMode::Spline) {
				const std::string splineName = context.clip.data.value("splineBinding", "");
				const auto splineEntity = splineName.empty() ? settings.spline.Resolve(world, context.target) : context.ResolveBinding(splineName);
				Matrix4x4 matrix;
				const auto* spline = TryGetSpline(world, splineEntity);
				if (spline && Resolve(splineEntity, context.time, state.clips.size(), matrix)) runtime.splineSamples = BuildSplineSamples(*spline, matrix);
			}
			const auto sample = EvaluateMotionAnimation(settings, runtime, GetMotionAnimationDuration(settings) *
				std::clamp(context.localTime / (std::max)(GetTimelineClipSourceDuration(context.clip), 0.0001), 0.0, 1.0));
			Matrix4x4 inverse;
			if (!Matrix4x4::TryInverse(parent, inverse)) return 0;
			Vector3 position;
			Quaternion rotation;
			DecomposeAffine3D(parent, position, rotation, scale);
			if (settings.applyPosition) local.localPos = Vector3::Lerp(local.localPos, Vector3::Transform(sample.position, inverse), context.weight);
			if (settings.applyRotation) local.localRotation = Quaternion::Lerp(local.localRotation,
				(Quaternion::Inverse(rotation) * sample.rotation).Normalize(), context.weight).Normalize();
			return static_cast<uint8_t>((settings.applyPosition ? 1 : 0) | (settings.applyRotation ? 2 : 0));
		}

		// Transform以外のプロパティやイベントは履歴サンプルで評価しない
		uint8_t Property(TimelineEvaluationContext& context, size_t index, TransformComponent& local) {

			const auto asset = ParseAssetID(context.clip.data, "asset");
			uint8_t affected = 0;
			std::vector<AnimationEvaluatedValue> values;
			if (asset) {
				const auto draft = GetAnimationClipAssetOverride(world, asset);
				const auto* source = draft.get();
				if (!source && systems.animationClipManager && systems.assetDatabase)
					source = systems.animationClipManager->GetOrLoad(*systems.assetDatabase, asset);
				if (!source) return 0;
				TransformComponent baseline = local;
				Matrix4x4 entry;
				if (Resolve(context.target, context.clip.start, index, entry)) {
					Matrix4x4 parent;
					if (Resolve(context.target, context.clip.start, 0, parent)) {
						const auto* transform = world.TryGetComponentForBinding<TransformComponent>(context.target);
						if (!transform) return 0;
						const auto identity = Baseline(context.target, *transform);
						Matrix4x4 inverseLocal, inverseParent;
						if (Matrix4x4::TryInverse(MakeLocalMatrix(identity), inverseLocal) && Matrix4x4::TryInverse(inverseLocal * parent, inverseParent))
							DecomposeAffine3D(entry * inverseParent, baseline.localPos, baseline.localRotation, baseline.localScale);
					}
				}
				std::vector<AnimationPreviewBaseValue> bases{
					{{"Transform", "localPos", AnimationValueType::Vector3}, baseline.localPos},
					{{"Transform", "localRotation", AnimationValueType::Quaternion}, baseline.localRotation},
					{{"Transform", "localScale", AnimationValueType::Vector3}, baseline.localScale}};
				AnimationClipEvaluator::EvaluateClipValues(world, context.target, *source,
					AnimationClipEvaluator::ResolveClipEvaluationTime(*source, static_cast<float>(context.localTime)), bases, values);
				if (source->relativeTransform) {
					std::vector<AnimationEvaluatedValue> neutral;
					AnimationClipEvaluator::EvaluateClipValues(world, context.target, *source, AnimationResolvedTime{}, bases, neutral);
					AnimationClipEvaluator::ComposeRelativeTransform(values, bases, neutral);
				}
			} else {
				const auto binding = context.clip.data.at("binding").get<AnimationPropertyBinding>();
				if (binding.componentName != "Transform") return 0;
				const auto& from = context.clip.data.at("from");
				const auto& to = context.clip.data.at("to");
				const float phase = EasedValue(context.clip.easing, static_cast<float>(std::clamp(context.localTime /
					(std::max)(GetTimelineClipSourceDuration(context.clip), 0.0001), 0.0, 1.0)));
				AnimationPropertyValue value;
				if (binding.valueType == AnimationValueType::Vector3) value = Vector3::Lerp(Vector3::FromJson(from), Vector3::FromJson(to), phase);
				else if (binding.valueType == AnimationValueType::Quaternion) value = Quaternion::Lerp(Quaternion::FromJson(from), Quaternion::FromJson(to), phase).Normalize();
				else return 0;
				values.push_back({binding, value});
			}
			for (const auto& value : values) {
				if (value.binding.componentName != "Transform") continue;
				const auto& path = value.binding.propertyPath;
				if (const auto* vector = std::get_if<Vector3>(&value.value)) {
					Vector3* current = path == "localPos" ? &local.localPos : path == "localScale" ? &local.localScale : nullptr;
					if (!current) continue;
					affected |= path == "localPos" ? 1 : 4;
					Vector3 next = *current;
					if (value.channelMask & 1) next.x = vector->x;
					if (value.channelMask & 2) next.y = vector->y;
					if (value.channelMask & 4) next.z = vector->z;
					*current = Vector3::Lerp(*current, next, context.weight);
				} else if (path == "localRotation") {
					if (const auto* rotation = std::get_if<Quaternion>(&value.value)) {
						affected |= 2;
						local.localRotation = Quaternion::Lerp(local.localRotation, *rotation, context.weight).Normalize();
					}
				}
			}
			return affected;
		}
	};
}

bool Engine::SampleTimelineTargetTransform(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	TimelinePlaybackState& state, SystemContext& context, double time, Entity target, Matrix4x4& result) {

	if (!std::isfinite(time)) return false;
	auto groups = state.groups;
	auto branches = state.branches;
	ScopedCleanup restore([&]() noexcept { state.groups = std::move(groups); state.branches = std::move(branches); });
	try {
		TransformSampler sampler{world, owner, player, state, context, {}, {}};
		return sampler.Resolve(target, std::clamp(time, 0.0, state.asset->duration), state.clips.size(), result);
	} catch (...) { return false; }
}

uint64_t Engine::GetTimelineSamplingRevision(ECSWorld& world, const TimelinePlaybackState& state, SystemContext& context) {

	uint64_t revision = reinterpret_cast<uintptr_t>(state.asset.get());
	const auto mix = [&](uint64_t value) { revision ^= value + 0x9E3779B97F4A7C15ULL + (revision << 6) + (revision >> 2); };
	if (const auto parent = state.samplingParent.lock()) mix(parent->samplingRevision);
	std::vector<Entity> entities;
	for (const auto& binding : state.bindings) {
		mix((static_cast<uint64_t>(binding.entity.generation) << 32) | binding.entity.index);
		if (std::find(entities.begin(), entities.end(), binding.entity) == entities.end()) entities.push_back(binding.entity);
	}
	for (size_t index = 0; index < entities.size(); ++index) {
		const auto entity = entities[index];
		const auto* transform = world.TryGetComponentForBinding<TransformComponent>(entity);
		if (!transform) continue;
		TransformComponent input = *transform;
		const auto channels = GetTimelineTransformChannels(world, state, entity, context);
		for (const auto& pose : state.previewPoses) {
			if (pose.entity != entity) continue;
			if (channels & 1) input.localPos = pose.position;
			if (channels & 2) input.localRotation = pose.rotation;
			if (channels & 4) input.localScale = pose.scale;
			break;
		}
		for (float number : {input.localPos.x, input.localPos.y, input.localPos.z, input.localRotation.x, input.localRotation.y,
			input.localRotation.z, input.localRotation.w, input.localScale.x, input.localScale.y, input.localScale.z}) mix(std::bit_cast<uint32_t>(number));
		mix(static_cast<uint32_t>(input.ignoreParentRotation) | (static_cast<uint32_t>(input.ignoreParentScale) << 1));
		const auto add = [&](Entity parent) {
			mix((static_cast<uint64_t>(parent.generation) << 32) | parent.index);
			if (world.IsAlive(parent) && std::find(entities.begin(), entities.end(), parent) == entities.end()) entities.push_back(parent);
		};
		if (const auto* hierarchy = world.TryGetComponentForBinding<HierarchyComponent>(entity)) add(hierarchy->parent);
		if (const auto* attachment = world.TryGetComponentForBinding<JointAttachmentComponent>(entity)) {
			mix(std::hash<std::string>{}(attachment->jointName));
			add(SceneObjectUtility::FindByLocalFileID(world, SceneObjectUtility::GetSceneInstanceID(world, entity), attachment->skinnedEntityLocalFileID));
		}
		if (const auto* skinned = TryGetSkinnedAnimationRuntime(world, entity)) mix(skinned->definitionGeneration);
	}
	if (state.settings) for (const auto& parameter : state.settings->parameters) {
		mix(parameter.parameter.value);
		std::visit([&](const auto& value) {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, Vector3>) {
				mix(std::bit_cast<uint32_t>(value.x)); mix(std::bit_cast<uint32_t>(value.y)); mix(std::bit_cast<uint32_t>(value.z));
			} else if constexpr (std::is_same_v<T, float>) mix(std::bit_cast<uint32_t>(value));
			else mix(std::hash<T>{}(value));
		}, parameter.value);
	}
	for (const auto& playback : state.clips) {
		if (playback.track->kind != TimelineTrackKind::Property) continue;
		const auto asset = ParseAssetID(playback.clip->data, "asset");
		if (!asset) continue;
		const auto source = context.animationClipManager ? GetResolvedAnimationClipAssetRevision(world, *context.animationClipManager, asset) :
			GetAnimationClipAssetOverrideRevision(world, asset);
		mix(source);
	}
	return revision;
}

uint8_t Engine::GetTimelineTransformChannels(ECSWorld& world, const TimelinePlaybackState& state, Entity target, SystemContext& context) {

	uint8_t channels = 0;
	const auto add = [&](const AnimationPropertyBinding& binding) {
		if (binding.componentName != "Transform") return;
		if (binding.propertyPath == "localPos" || binding.propertyPath == "localPos2D") channels |= 1;
		if (binding.propertyPath == "localRotation" || binding.propertyPath == "localRotationZ") channels |= 2;
		if (binding.propertyPath == "localScale" || binding.propertyPath == "localScale2D") channels |= 4;
	};
	for (const auto& playback : state.clips) {
		if (ResolveTarget(state, playback) != target) continue;
		if (playback.track->kind == TimelineTrackKind::Camera) { channels = 7; continue; }
		if (playback.track->kind == TimelineTrackKind::Motion) {
			MotionAnimationSettings settings;
			if (playback.clip->data.contains("settings")) settings = playback.clip->data.at("settings").get<MotionAnimationSettings>();
			if (settings.applyPosition) channels |= 1;
			if (settings.applyRotation) channels |= 2;
			continue;
		}
		if (playback.track->kind != TimelineTrackKind::Property) continue;
		const auto asset = ParseAssetID(playback.clip->data, "asset");
		if (!asset) { add(playback.clip->data.at("binding").get<AnimationPropertyBinding>()); continue; }
		const auto draft = GetAnimationClipAssetOverride(world, asset);
		const auto* source = draft.get();
		if (!source && context.animationClipManager && context.assetDatabase) source = context.animationClipManager->GetOrLoad(*context.assetDatabase, asset);
		if (source) for (const auto& track : source->curveTracks) add(track.binding);
	}
	return channels;
}
