#include "TimelineBuiltinEvaluation.h"
#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Systems/Animation/SkeletonPoseEvaluator.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <exception>

using namespace Engine;

namespace {

	struct PropertySample {

		Engine::UUID instance{};
		AnimationPropertyValue value{};
		float weight = 0;
	};
	struct PropertyMix {

		Entity target{};
		AnimationPropertyDescriptor descriptor;
		AnimationPropertyValue baseline{}, accumulated{};
		float weight = 0;
		uint32_t references = 0;
		bool existed = true;
		std::vector<PropertySample> samples;
	};
	struct PoseSample {

		std::string_view clip;
		float time = 0, weight = 0;
		bool loop = false;
		bool paused = false;
		Engine::UUID instance{};
	};
	struct PoseMix {

		Entity target{};
		std::vector<PoseSample> samples;
	};
	struct BuiltinMixStorage {

		uint32_t depth = 0;
		std::vector<PropertyMix> properties;
		std::vector<PoseMix> poses;
		std::vector<std::function<void()>> exits;
		JointAttachmentSystem attachments;
		std::vector<Entity> changedTransforms;
	};

	// 同じ対象とプロパティの共有作業領域を見つける
	auto FindProperty(BuiltinMixStorage& storage, Entity target, const AnimationPropertyDescriptor& descriptor) {

		return std::find_if(storage.properties.begin(), storage.properties.end(), [&](const PropertyMix& entry) {
			return (entry.target == target || descriptor.componentName == "GlobalTime") && entry.descriptor.componentName == descriptor.componentName &&
				entry.descriptor.propertyPath == descriptor.propertyPath;
		});
	}

	// 重みを正規化し、通常再生のポーズも含めて骨格を合成する
	void FlushPose(ECSWorld& world, SystemContext& context, PoseMix& mix) {

		if (mix.samples.empty() || !world.IsAlive(mix.target)) return;
		bool changed = false;
		const auto definition = PrepareSkinnedAnimation(world, mix.target, context, changed);
		auto* runtime = TryGetSkinnedAnimationRuntime(world, mix.target);
		if (!definition || !runtime) return;
		float sum = 0;
		for (const auto& sample : mix.samples) sum += sample.weight;
		const bool hasUnderlay = sum < 1;
		if (hasUnderlay) mix.samples.push_back({runtime->currentClip, runtime->time, 1 - sum, false, mix.samples.front().paused});
		float accumulated = 0;
		uint32_t contributions = 0;
		runtime->skeleton = runtime->bindSkeleton;
		const PoseSample* dominant = nullptr;
		std::string_view dominantName;
		float dominantTime = 0;
		for (const auto& sample : mix.samples) {
			if (sample.weight <= 0) continue;
			const std::string name = sample.clip.empty() ? runtime->currentClip : std::string(sample.clip);
			const auto tracks = definition->clipJointTracks.find(name);
			const auto clip = definition->clips.find(name);
			if (tracks == definition->clipJointTracks.end() || clip == definition->clips.end()) continue;
			float time = (std::max)(sample.time, 0.0f);
			if (sample.loop && clip->second.duration > 0) time = std::fmod(time, clip->second.duration);
			else time = (std::min)(time, clip->second.duration);
			runtime->timelineScratchSkeleton = runtime->bindSkeleton;
			SkeletonPoseEvaluator::ApplyClipToSkeleton(runtime->timelineScratchSkeleton, tracks->second, time);
			const float t = sample.weight / (accumulated + sample.weight);
			for (size_t i = 0; i < runtime->skeleton.joints.size(); ++i) {
				auto& current = runtime->skeleton.joints[i].transform;
				const auto& next = runtime->timelineScratchSkeleton.joints[i].transform;
				current.translation = Vector3::Lerp(current.translation, next.translation, t);
				current.scale = Vector3::Lerp(current.scale, next.scale, t);
				current.rotation = Quaternion::Lerp(current.rotation, next.rotation, t).Normalize();
			}
			accumulated += sample.weight;
			++contributions;
			if (!dominant || dominant->weight <= sample.weight) { dominant = &sample; dominantName = clip->first; dominantTime = time; }
		}
		if (dominant) {
			runtime->timelineControlled = true;
			runtime->timelineClip = dominantName;
			runtime->timelineTime = dominantTime;
			runtime->timelineDuration = definition->clips.at(std::string(dominantName)).duration;
			runtime->timelineFinished = !dominant->loop && dominant->time >= runtime->timelineDuration;
			runtime->timelineInTransition = contributions > 1;
			runtime->timelinePlaying = !dominant->paused && !runtime->timelineFinished;
		}
		SkeletonPoseEvaluator::UpdateSkeletonHierarchy(runtime->skeleton);
		SkeletonPoseEvaluator::BuildPalette(runtime->skeleton, definition->skinCluster, runtime->palette);
		++runtime->poseGeneration;
		if (hasUnderlay) mix.samples.pop_back();
	}
}

void Engine::BeginBuiltinTimelineEvaluation(ECSWorld& world) {

	auto& storage = world.GetStorage().Get<BuiltinMixStorage>();
	if (storage.depth++ != 0) return;
	for (auto& entry : storage.properties) { entry.weight = 0; entry.samples.clear(); }
	for (auto& entry : storage.poses) entry.samples.clear();
}

void Engine::FlushBuiltinTimelineEvaluation(ECSWorld& world, SystemContext& context) {

	auto& storage = world.GetStorage().Get<BuiltinMixStorage>();
	if (!storage.depth || --storage.depth != 0) return;
	std::exception_ptr firstError;
	try { CommitBuiltinTimelineSamples(world, context); } catch (...) { firstError = std::current_exception(); }
	auto exits = std::move(storage.exits);
	storage.exits.clear();
	for (auto& cleanup : exits) {
		try { cleanup(); } catch (...) { if (!firstError) firstError = std::current_exception(); }
	}
	std::erase_if(storage.properties, [](const PropertyMix& entry) { return entry.references == 0; });
	std::erase_if(storage.poses, [&](const PoseMix& entry) { return !world.IsAlive(entry.target); });
	if (firstError) std::rethrow_exception(firstError);
}

void Engine::CommitBuiltinTimelineSamples(ECSWorld& world, SystemContext& context) {

	auto& storage = world.GetStorage().Get<BuiltinMixStorage>();
	for (auto& entry : storage.properties) {
		entry.weight = 0;
		for (const auto& sample : entry.samples) {
			if (sample.weight <= 0) continue;
			if (entry.weight == 0) entry.accumulated = sample.value;
			else AnimationValueOperations::LerpValue(entry.accumulated, sample.value,
				sample.weight / (entry.weight + sample.weight), entry.accumulated);
			entry.weight += sample.weight;
		}
		if (entry.weight <= 0 || (entry.descriptor.componentName != "GlobalTime" && !world.IsAlive(entry.target))) continue;
		AnimationPropertyValue value = entry.accumulated;
		if (entry.weight < 1) AnimationValueOperations::LerpValue(entry.baseline, value, entry.weight, value);
		entry.descriptor.setValue(world, entry.target, value);
	}
	UpdateBuiltinTimelineRealtime(world, 0.0f);
	for (auto& entry : storage.poses) FlushPose(world, context, entry);
}

void Engine::DeferBuiltinTimelineExit(ECSWorld& world, std::function<void()> cleanup) {

	auto& storage = world.GetStorage().Get<BuiltinMixStorage>();
	if (storage.depth) storage.exits.push_back(std::move(cleanup)); else cleanup();
}

void Engine::RetireBuiltinTimelineSamples(ECSWorld& world, const TimelineControlOwner& owner) {

	auto* storage = world.GetStorage().TryGet<BuiltinMixStorage>();
	if (!storage) return;
	for (auto& entry : storage->properties)
		std::erase_if(entry.samples, [&](const PropertySample& sample) { return sample.instance == owner.instance; });
	for (auto& entry : storage->poses)
		std::erase_if(entry.samples, [&](const PoseSample& sample) { return sample.instance == owner.instance; });
}

void Engine::UpdateTimelineAttachmentsForPhysics(ECSWorld& world, SystemContext& context) {

	if (!world.GetStorage().TryGet<BuiltinMixStorage>()) return;
	auto& storage = world.GetStorage().Get<BuiltinMixStorage>();
	storage.changedTransforms.clear();
	bool controlled = false;
	ComponentChangeChannel channels = ComponentChangeChannel::None;
	world.ForEach<SkinnedAnimationComponent, TransformComponent>([&](Entity entity, SkinnedAnimationComponent&, TransformComponent& transform) {
		const auto* runtime = TryGetSkinnedAnimationRuntime(world, entity);
		if (!runtime || (!runtime->timelineControlled && !IsTimelineControlled(world, entity, "Transform"))) return;
		controlled = true;
		ResolvedWorldTransform resolved;
		if (!TransformWorldUtility::ResolveWorldTransform(world, entity, resolved, true) || transform.worldMatrix == resolved.matrix) return;
		transform.worldMatrix = resolved.matrix;
		transform.isDirty = false;
		channels |= world.GetTransformChangeChannels(entity);
		storage.changedTransforms.push_back(entity);
	});
	if (!storage.changedTransforms.empty()) {
		world.MarkDataModified();
		world.MarkTransformConsumersModified(channels, storage.changedTransforms);
	}
	if (controlled) storage.attachments.LateUpdate(world, context);
}

void Engine::QueueTimelineSkinnedSample(TimelineEvaluationContext& context, const SkinnedAnimationSample& sample) {

	auto& storage = context.world.GetStorage().Get<BuiltinMixStorage>();
	auto it = std::find_if(storage.poses.begin(), storage.poses.end(), [&](const PoseMix& mix) { return mix.target == context.target; });
	if (it == storage.poses.end()) { storage.poses.push_back({context.target, {}}); it = std::prev(storage.poses.end()); }
	const auto previous = std::find_if(it->samples.begin(), it->samples.end(), [&](const PoseSample& value) {
		return value.instance == context.control.instance;
	});
	const PoseSample value{sample.clip, sample.time, context.weight, sample.loop, context.paused, context.control.instance};
	if (previous == it->samples.end()) it->samples.push_back(value); else *previous = value;
}

AnimationPropertyValue Engine::CaptureTimelinePropertyBase(TimelineEvaluationContext& context,
	const AnimationPropertyDescriptor& descriptor) {

	auto& storage = context.world.GetStorage().Get<BuiltinMixStorage>();
	auto it = FindProperty(storage, context.target, descriptor);
	if (it == storage.properties.end()) {
		AnimationPropertyValue value;
		descriptor.getValue(context.world, context.target, value);
		storage.properties.push_back({context.target, descriptor, value, value, 0, 0});
		it = std::prev(storage.properties.end());
		it->existed = !descriptor.hasValue || descriptor.hasValue(context.world, context.target);
	}
	if (!it->references) {
		descriptor.getValue(context.world, context.target, it->baseline);
		it->existed = !descriptor.hasValue || descriptor.hasValue(context.world, context.target);
	}
	if (context.playbackState) {
		auto& bases = context.playbackState->propertyBases;
		const auto found = std::find_if(bases.begin(), bases.end(), [&](const auto& base) {
			return base.entity == context.target && base.descriptor.componentName == descriptor.componentName &&
				base.descriptor.propertyPath == descriptor.propertyPath && base.descriptor.valueType == descriptor.valueType;
		});
		if (found == bases.end()) bases.push_back({context.target, descriptor, it->baseline, it->existed});
		else if (context.isScrubbing && !it->references) { it->baseline = found->value; it->existed = found->present; }
	}
	++it->references;
	return it->baseline;
}

bool Engine::TryGetTimelinePropertyBase(const ECSWorld& world, Entity entity,
	const AnimationPropertyBinding& binding, AnimationPropertyValue& value) {

	const auto* storage = world.GetStorage().TryGet<BuiltinMixStorage>();
	if (!storage) return false;
	const auto found = std::find_if(storage->properties.begin(), storage->properties.end(), [&](const auto& entry) {
		return entry.references && (entry.target == entity || binding.componentName == "GlobalTime") &&
			entry.descriptor.componentName == binding.componentName && entry.descriptor.propertyPath == binding.propertyPath &&
			entry.descriptor.valueType == binding.valueType;
	});
	if (found == storage->properties.end()) return false;
	value = found->baseline;
	return true;
}

void Engine::QueueTimelinePropertySample(TimelineEvaluationContext& context, const AnimationPropertyDescriptor& descriptor,
	[[maybe_unused]] const AnimationPropertyValue& baseline, const AnimationPropertyValue& value) {

	auto& storage = context.world.GetStorage().Get<BuiltinMixStorage>();
	auto it = FindProperty(storage, context.target, descriptor);
	if (it == storage.properties.end()) return;
	const auto previous = std::find_if(it->samples.begin(), it->samples.end(), [&](const PropertySample& sample) {
		return sample.instance == context.control.instance;
	});
	const PropertySample sample{context.control.instance, value, context.weight};
	if (previous == it->samples.end()) it->samples.push_back(sample); else *previous = sample;
}

void Engine::RestoreTimelinePropertyBase(TimelineEvaluationContext& context,
	const AnimationPropertyDescriptor& descriptor, bool restore) {

	auto& storage = context.world.GetStorage().Get<BuiltinMixStorage>();
	const auto it = FindProperty(storage, context.target, descriptor);
	if (it == storage.properties.end()) return;
	if (it->references) --it->references;
	if (!it->references && restore && (descriptor.componentName == "GlobalTime" || context.world.IsAlive(context.target)) &&
		CanRestoreTimelineControl(context.world, context.target, descriptor.componentName, descriptor.propertyPath, context.control)) {
		if (!it->existed && descriptor.clearValue) descriptor.clearValue(context.world, context.target);
		else descriptor.setValue(context.world, context.target, it->baseline);
	}
}
