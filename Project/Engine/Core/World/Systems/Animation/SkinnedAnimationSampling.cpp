#include "SkinnedAnimationSampling.h"

//============================================================================
//	include
//============================================================================
#include "SkeletonPoseEvaluator.h"
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

std::string Engine::ResolveSkinnedAnimationClip(const SkinnedMeshAnimationSet& animationSet,
	std::string_view requestedClip) {

	if (!requestedClip.empty() && animationSet.clips.contains(std::string(requestedClip))) return std::string(requestedClip);
	if (animationSet.clips.contains("Default")) return "Default";
	if (!animationSet.clipOrder.empty()) return animationSet.clipOrder.front();
	return animationSet.clips.empty() ? std::string{} : animationSet.clips.begin()->first;
}

std::shared_ptr<const SkinnedMeshAnimationSet> Engine::PrepareSkinnedAnimation(ECSWorld& world, Entity entity,
	SystemContext& context, bool& definitionChanged) {

	definitionChanged = false;
	auto* renderer = world.TryGetComponentForBinding<MeshRendererComponent>(entity);
	auto* animation = world.TryGetComponentForBinding<SkinnedAnimationComponent>(entity);
	auto* runtime = TryGetSkinnedAnimationRuntime(world, entity);
	if (!renderer || !animation || !runtime || !context.skinnedAnimationManager || !context.assetDatabase) return {};
	if (!renderer->mesh) {
		runtime->palette.clear();
		runtime->availableClips.clear();
		runtime->initialized = false;
		runtime->currentDuration = 0;
		return {};
	}
	context.skinnedAnimationManager->RequestLoadAsync(*context.assetDatabase, renderer->mesh);
	auto animationSet = context.skinnedAnimationManager->Find(renderer->mesh);
	if (!animationSet || !animationSet->valid || animationSet->clips.empty()) return {};
	definitionChanged = !runtime->initialized || runtime->mesh != renderer->mesh ||
		runtime->definitionGeneration != animationSet->generation;
	if (!definitionChanged) return animationSet;
	runtime->currentClip = ResolveSkinnedAnimationClip(*animationSet, animation->clip);
	runtime->mesh = renderer->mesh;
	runtime->definitionGeneration = animationSet->generation;
	runtime->initialized = true;
	runtime->bindSkeleton = animationSet->skeleton;
	runtime->skeleton = runtime->bindSkeleton;
	runtime->palette.resize(animationSet->skeleton.joints.size());
	runtime->fromClip.clear();
	runtime->toClip.clear();
	runtime->time = animation->playbackSpeed < 0 ? animationSet->clips.at(runtime->currentClip).duration : 0;
	runtime->currentDuration = animationSet->clips.at(runtime->currentClip).duration;
	runtime->fromTime = 0;
	runtime->toTime = 0;
	runtime->blendTime = 0;
	runtime->inTransition = false;
	runtime->animationFinished = false;
	runtime->repeatCount = 0;
	runtime->availableClips = animationSet->clipOrder;
	return animationSet;
}

bool Engine::SampleSkinnedAnimation(ECSWorld& world, Entity entity, SystemContext& context,
	const SkinnedAnimationSample& sample, std::string* diagnostic) {

	bool changed = false;
	const auto animationSet = PrepareSkinnedAnimation(world, entity, context, changed);
	if (!animationSet) {
		if (diagnostic) *diagnostic = "スキニングアニメーションの読み込み待ちです";
		return false;
	}
	auto* runtime = TryGetSkinnedAnimationRuntime(world, entity);
	const std::string clip = sample.clip.empty() ? runtime->currentClip : std::string(sample.clip);
	const auto track = animationSet->clipJointTracks.find(clip);
	const auto clipData = animationSet->clips.find(clip);
	if (track == animationSet->clipJointTracks.end() || clipData == animationSet->clips.end()) {
		if (diagnostic) *diagnostic = "指定した骨アニメーションクリップがありません: " + clip;
		return false;
	}
	auto resolveTime = [&](float time, float duration) {
		if (!std::isfinite(time) || duration <= 0) return 0.0f;
		return sample.loop ? std::fmod((std::max)(time, 0.0f), duration) : std::clamp(time, 0.0f, duration);
	};
	const float time = resolveTime(sample.time, clipData->second.duration);
	runtime->skeleton = runtime->bindSkeleton;
	const auto from = animationSet->clipJointTracks.find(std::string(sample.fromClip));
	const auto fromData = animationSet->clips.find(std::string(sample.fromClip));
	if (sample.weight < 1 && from != animationSet->clipJointTracks.end() && fromData != animationSet->clips.end()) {
		SkeletonPoseEvaluator::BlendClipsToSkeleton(runtime->skeleton, from->second,
			resolveTime(sample.fromTime, fromData->second.duration), track->second, time, std::clamp(sample.weight, 0.0f, 1.0f));
	} else {
		SkeletonPoseEvaluator::ApplyClipToSkeleton(runtime->skeleton, track->second, time);
	}
	SkeletonPoseEvaluator::UpdateSkeletonHierarchy(runtime->skeleton);
	SkeletonPoseEvaluator::BuildPalette(runtime->skeleton, animationSet->skinCluster, runtime->palette);
	runtime->timelineControlled = true;
	runtime->timelineClip = clip;
	runtime->timelineTime = time;
	runtime->timelineDuration = clipData->second.duration;
	runtime->timelineFinished = !sample.loop && sample.time >= clipData->second.duration;
	runtime->timelineInTransition = sample.weight < 1;
	runtime->timelinePlaying = !runtime->timelineFinished;
	++runtime->poseGeneration;
	if (diagnostic) diagnostic->clear();
	return true;
}
