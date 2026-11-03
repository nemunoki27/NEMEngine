#include "AnimationClipPlayerSystem.h"

//============================================================================
//	include
//============================================================================
#include "AnimationEventCollection.h"
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyCatalog.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>
#include <utility>

void Engine::AnimationClipPlayerSystem::RestoreEditPreviews(ECSWorld& world) {

	for (auto& [id, state] : world.GetStorage().Get<PlaybackStorage>().values) {
		if (!state.edit) continue;
		const auto entity = world.FindByUUID(id);
		if (world.IsAlive(entity)) AnimationPropertySnapshot::Restore(world, entity, state.base);
		state = {};
	}
}

void Engine::AnimationClipPlayerSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditPreviews(world);
	world.GetStorage().Get<PlaybackStorage>().values.clear();
}

void Engine::AnimationClipPlayerSystem::Update(ECSWorld& world, SystemContext& context) {

	SetAnimationMaterialContext(world, context.assetDatabase, context.renderAssetLibrary);

	auto& states = world.GetStorage().Get<PlaybackStorage>().values;
	for (auto& [id, state] : states) state.visited = false;
	world.ForEach<AnimationClipPlayerComponent>([&](Entity entity, AnimationClipPlayerComponent& player) {
		player.runtimeApplied = false;
		if (context.IsUpdateInterrupted()) return;
		if (!player.enabled || !IsEntityActiveInHierarchy(world, entity) ||
			(context.mode == WorldMode::Edit && !player.playInEditMode)) {
			player.runtimePlaying = false;
			return;
		}
		UpdatePlayer(world, entity, player, context);
	});
	for (auto it = states.begin(); it != states.end();) {
		if (it->second.visited) { ++it; continue; }
		const auto entity = world.FindByUUID(it->first);
		if (it->second.edit && world.IsAlive(entity)) AnimationPropertySnapshot::Restore(world, entity, it->second.base);
		it = states.erase(it);
	}
}

void Engine::AnimationClipPlayerSystem::UpdatePlayer(ECSWorld& world, Entity entity,
	AnimationClipPlayerComponent& player, SystemContext& context) {

	using namespace AnimationPlaybackTime;
	if (!context.assetDatabase || !context.animationClipManager) return;
	auto& state = world.GetStorage().Get<PlaybackStorage>().values[world.GetUUID(entity)];
	state.visited = true;
	state.edit = context.mode == WorldMode::Edit;
	const auto* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, player.clip);
	if (state.clip != player.clip || state.revision != context.animationClipManager->GetRevision(player.clip)) {
		player.runtimeApplied |= !state.base.empty();
		AnimationPropertySnapshot::Restore(world, entity, state.base);
		state.base.clear();
		state.initialized = false;
		state.clip = player.clip;
		state.revision = context.animationClipManager->GetRevision(player.clip);
	}
	player.runtimeDiagnostic = clip ? "" : "アニメーションクリップを設定してください";
	if (!clip) { player.runtimePlaying = false; return; }
	const float duration = (std::max)(clip->duration, 0.001f);
	player.runtimeDuration = clip->duration;
	const int32_t request = std::exchange(player.runtimeRequest, 0);
	auto& runtime = state.playback;
	bool evaluate = false;
	if (!state.initialized || request == 1 || request == 5) {
		player.runtimeApplied |= !state.base.empty();
		AnimationPropertySnapshot::Restore(world, entity, state.base);
		state.base.clear();
		AnimationPropertySnapshot::CaptureClip(world, entity, *clip, clip->relativeTransform, state.base);
		runtime = {};
		runtime.delayRemaining = (std::max)(player.startDelay, 0.0f);
		runtime.started = runtime.delayRemaining == 0;
		runtime.playing = request == 1 || (request != 5 && (state.edit || player.playOnStart));
		player.runtimePaused = false;
		state.initialized = true;
		evaluate = runtime.playing && runtime.started;
	}
	if (request == 2) player.runtimePaused = true;
	if (request == 3) { player.runtimePaused = false; if (!runtime.finished) runtime.playing = true; }
	if (request == 4) { runtime.playing = false; player.runtimePaused = false; }
	if (request == 6 && std::isfinite(player.runtimeSeekTime)) {
		runtime.time = (std::clamp)(player.runtimeSeekTime, 0.0f, clip->duration);
		runtime.phase = AnimationClipPhase::Play;
		runtime.phaseTime = 0;
		runtime.finished = false;
		runtime.started = runtime.sampled = true;
		runtime.eventStartPending = false;
		evaluate = true;
	}
	std::vector<AnimationPlaybackTime::Interval> traversed;
	std::vector<AnimationEvent> events;
	auto source = player.timeSource;
	if (player.unscaledTime) source.mode = TimeSourceMode::Unscaled;
	const auto localTime = ResolveLocalTime(world, entity, source, TimeChannel::AnimationClip,
		state.edit ? context.unscaledDeltaTime : context.deltaTime, context.unscaledDeltaTime);
	if (!localTime.valid) player.runtimeDiagnostic = "時間源のTimeScaleが未設定です";
	float delta = localTime.deltaTime;
	if (runtime.playing && !player.runtimePaused && request != 5 && request != 6 && std::isfinite(delta) && delta > 0) {
		if (!runtime.started) {
			const float waiting = (std::min)(delta, runtime.delayRemaining);
			delta -= waiting;
			runtime.delayRemaining -= waiting;
			runtime.started = runtime.delayRemaining <= 0;
		}
		const float step = delta * player.speed;
		if (runtime.started && std::isfinite(step) && step > 0) {
			AnimationPlaybackSettings settings;
			settings.loopCount = settings.pingPongCount = player.repeatCount;
			settings.interval = player.interval;
			settings.loopBridge = clip->loopBridge;
			auto* traversal = !state.edit && !clip->events.empty() ? &traversed : nullptr;
			const auto wrap = EffectiveWrap(player.wrapMode, clip->loop);
			if (wrap == AnimationWrapMode::Loop) AdvanceLoop(runtime, settings, duration, step, traversal);
			else if (wrap == AnimationWrapMode::PingPong) AdvancePingPong(runtime, settings, duration, step, traversal);
			else {
				const float before = runtime.time;
				runtime.time = AdvanceTime(duration, wrap, runtime.time, runtime.dir, step, runtime.finished);
				RecordInterval(runtime, before, runtime.time, traversal);
				if (runtime.finished) runtime.playing = false;
			}
			evaluate = true;
		}
	}
	if (evaluate) {
		AnimationResolvedTime time;
		time.clipTime = runtime.time;
		time.inLoopBridge = runtime.phase == AnimationClipPhase::Bridge;
		time.bridgeT = runtime.phaseTime / (std::max)(clip->loopBridge.duration, 0.001f);
		std::vector<AnimationEvaluatedValue> values;
		AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, time, state.base, values);
		if (clip->relativeTransform) {
			std::vector<AnimationEvaluatedValue> neutral;
			AnimationClipEvaluator::EvaluateClipValues(world, entity, *clip, AnimationResolvedTime{}, state.base, neutral);
			AnimationClipEvaluator::ComposeRelativeTransform(values, state.base, neutral);
		} else AnimationClipEvaluator::PreserveUnkeyedChannels(world, entity, *clip, values);
		AnimationClipEvaluator::WriteValues(world, entity, values);
		player.runtimeApplied |= !values.empty();
	}
	player.runtimeTime = runtime.time;
	player.runtimePlaying = runtime.playing && !player.runtimePaused;
	player.runtimeFinished = runtime.finished;
	AnimationEventCollection::CollectClipEvents(*clip, traversed, events);
	for (const auto& event : events) {
		if (context.IsUpdateInterrupted() || !world.IsAlive(entity)) break;
		BehaviorSystem::DispatchAnimationEvent(world, context, entity, event.name, event.floatParam, event.intParam, event.stringParam);
	}
}
