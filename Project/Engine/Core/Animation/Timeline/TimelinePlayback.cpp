#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include "TimelinePlayerAccess.h"
#include "TimelineCollisionGate.h"
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	struct EvaluationStack {

		std::vector<const TimelineAsset*> assets;
	};

	// 通知を消費しないアプリでも無制限にメモリを増やさない
	void Emit(TimelinePlayerRecord& player, const TimelineClipPlayback& playback, double time) {

		if (player.runtime.notifications.size() >= 4096) {
			player.runtime.diagnostic = "Timeline通知が未取得のため上限へ達しました";
			return;
		}
		auto notification = playback.notification;
		notification.time = time;
		notification.target = playback.target;
		player.runtime.notifications.push_back(std::move(notification));
	}
}

void Engine::ExitTimelineClip(ECSWorld& world, Entity owner, const std::shared_ptr<TimelinePlaybackState>& state,
	TimelineClipPlayback& clip, SystemContext& context, double time, TimelineExitReason reason) {

	if (!clip.entered) return;
	CommitBuiltinTimelineSamples(world, context);
	if (clip.instance) {
		auto evaluation = MakeTimelineEvaluationContext(world, owner, *state, clip, context, time, 0.0);
		clip.instance->Retire(evaluation);
	}
	RetireBuiltinTimelineSamples(world, clip.control);
	clip.entered = false;
	auto systems = context;
	systems.updateInterruption = {};
	auto retired = std::make_shared<TimelineClipPlayback>();
	retired->track = clip.track;
	retired->clip = clip.clip;
	const bool retain = state->isScrubbing && clip.track->kind == TimelineTrackKind::Motion &&
		(reason == TimelineExitReason::Seek || reason == TimelineExitReason::Completed || reason == TimelineExitReason::Stopped);
	retired->instance = retain ? clip.instance : std::move(clip.instance);
	retired->control = clip.control;
	retired->target = clip.target;
	auto bindings = state->bindings;
	const auto definition = state->asset;
	const auto mode = state->mode;
	DeferBuiltinTimelineExit(world, [&, state, retired, owner, systems, time, reason, definition, mode,
		bindings = std::move(bindings)]() mutable {
		ScopedCleanup release([&]() noexcept { ReleaseTimelineControls(world, retired->control); });
		if (retired->instance) {
			auto evaluation = MakeTimelineEvaluationContext(world, owner, *state, *retired, systems, time, 0.0, definition.get());
			evaluation.bindings = bindings;
			evaluation.mode = mode;
			retired->instance->Exit(evaluation, reason);
		}
	});
	clip.control.instance = Engine::UUID::New();
}

namespace {

bool EvaluateTimelineSample(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	const TimelineAsset& asset, double time, SystemContext& context, TimelineEvaluationMode mode, bool emitEvents, bool isScrubbing) {

	if (!std::isfinite(time) || !world.IsAlive(owner) || world.IsPendingDestroy(owner)) return false;
	auto& stack = world.GetStorage().Get<EvaluationStack>().assets;
	if (stack.size() >= 32 || std::any_of(stack.begin(), stack.end(), [&](const auto* previous) {
		return previous == &asset || (asset.guid && previous->guid == asset.guid);
	})) {
		player.runtime.diagnostic = "子Timelineの参照が循環しているか階層が深すぎます";
		return false;
	}
	stack.push_back(&asset);
	ScopedCleanup pop([&]() noexcept { stack.pop_back(); });
	auto state = player.runtime.playback;
	if (state && ((state->mode == TimelineEvaluationMode::Preview) != (mode == TimelineEvaluationMode::Preview) ||
		(!isScrubbing && time < state->evaluatedTime))) {
		ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::Seek);
		state.reset();
	}
	if (!state) {
		if (!PrepareTimelinePlayback(world, owner, player, asset, context, mode)) return false;
		state = player.runtime.playback;
		state->root = stack.size() == 1;
	}
	if (state->source != &asset && !RefreshTimelinePlayback(world, owner, player, asset, context)) return false;
	const bool refreshed = std::exchange(state->refreshed, false);
	if (refreshed) emitEvents = false;
	const auto evaluationMode = refreshed && mode == TimelineEvaluationMode::Playback ? TimelineEvaluationMode::Seek : mode;
	state->lastContext = context;
	state->lastContext.updateInterruption = {};
	state->mode = mode;
	state->isScrubbing = isScrubbing;
	state->settings = &player.settings;
	state->paused = player.runtime.paused || player.runtime.preparing || (mode == TimelineEvaluationMode::Playback &&
		player.settings.timeSource.mode != TimeSourceMode::Unscaled && context.deltaTime <= 0.0f && context.unscaledDeltaTime > 0.0f);
	const double duration = state->asset->duration;
	const double current = std::abs(time - duration) < 1e-9 ? duration : std::clamp(time, 0.0, duration);
	const double previous = state->evaluatedTime;
	const bool first = !state->evaluated;
	const double delta = first || isScrubbing ? 0.0 : (std::max)(0.0, current - previous);
	if (isScrubbing) {
		emitEvents = false;
		player.runtime.pendingNotifications.clear();
		state->groups.clear();
		state->branches.clear();
		for (auto& playback : state->clips) playback.pendingEvent = false;
		for (auto& playback : state->clips) {
			if (!playback.entered) continue;
			const auto& clip = *playback.clip;
			const bool active = clip.enabled && current >= clip.start && current <= clip.start + clip.duration &&
				IsTimelineTrackActive(world, owner, player, *state, *playback.track, current);
			if (!active) ExitTimelineClip(world, owner, state, playback, context, previous, TimelineExitReason::Seek);
		}
		for (const auto& base : state->propertyBases) {
			if (!world.IsAlive(base.entity) && base.descriptor.componentName != "GlobalTime") continue;
			const auto& descriptor = base.descriptor;
			const TimelineControlOwner control{player.owner};
			if (IsTimelineControlled(world, base.entity, descriptor.componentName, descriptor.propertyPath) &&
				!OwnsTimelineControl(world, base.entity, descriptor.componentName, descriptor.propertyPath, control)) continue;
			if (!base.present && descriptor.clearValue) descriptor.clearValue(world, base.entity);
			else descriptor.setValue(world, base.entity, base.value);
		}
		for (const auto& pose : state->previewPoses) {
			if (auto* transform = world.TryGetComponentForBinding<TransformComponent>(pose.entity)) {
				const auto channels = GetTimelineTransformChannels(world, *state, pose.entity, context);
				const TimelineControlOwner control{player.owner};
				const auto writable = [&](std::string_view property) {
					return !IsTimelineControlled(world, pose.entity, "Transform", property) ||
						OwnsTimelineControl(world, pose.entity, "Transform", property, control);
				};
				if ((channels & 1) && writable("localPos")) transform->localPos = pose.position;
				if ((channels & 2) && writable("localRotation")) transform->localRotation = pose.rotation;
				if ((channels & 4) && writable("localScale")) transform->localScale = pose.scale;
				MarkTransformSubtreeDirty(world, pose.entity);
			}
		}
	}
	bool bindingsChanged = false;
	for (auto& binding : state->bindings) {
		const bool configured = std::any_of(player.settings.bindings.begin(), player.settings.bindings.end(),
			[&](const auto& value) { return value.slot == binding.id; });
		const auto target = !configured && binding.name == "Actor" ? owner :
			ResolveTimelineBinding(world, owner, player.settings, binding.id);
		bindingsChanged |= target != binding.entity;
		binding.entity = target;
	}
	if (bindingsChanged) {
		RefreshTimelineCollisionGates(world, *state);
		CaptureTimelinePreviewPoses(world, *state);
	}
	if (std::any_of(state->clips.begin(), state->clips.end(), [](const auto& clip) {
		return clip.track->kind == TimelineTrackKind::Particle || clip.track->kind == TimelineTrackKind::SubTimeline;
	}))
		state->samplingRevision = GetTimelineSamplingRevision(world, *state, context);
	player.runtime.windows.clear();
	ApplyTimelineCollisionGates(world, *state);
	BeginBuiltinTimelineEvaluation(world);
	ScopedCleanup flush([&]() noexcept {
		try { FlushBuiltinTimelineEvaluation(world, context); }
		catch (const std::exception& error) { player.runtime.diagnostic = error.what(); }
	});
	const auto configureSampling = [&](TimelineEvaluationContext& evaluation) {
		if (evaluation.track.kind != TimelineTrackKind::Particle && evaluation.track.kind != TimelineTrackKind::SubTimeline) return;
		evaluation.sampleTargetTransform = [&, weakState = std::weak_ptr<TimelinePlaybackState>(state)](double sampleTime, Entity target, Matrix4x4& result) {
			const auto currentState = weakState.lock();
			return currentState && SampleTimelineTargetTransform(world, owner, player, *currentState, context, sampleTime, target, result);
		};
	};
	try {
		bool boundaryPreparing = false;
		for (auto& playback : state->clips) {
			const auto& clip = *playback.clip;
			Entity target = Entity::Null();
			for (const auto& binding : state->bindings)
				if (binding.id == playback.track->binding) target = binding.entity;
			if (target != playback.target) {
				ExitTimelineClip(world, owner, state, playback, context, previous, TimelineExitReason::BindingLost);
				playback.target = target;
				if (!playback.instance) playback.instance = MakeTimelineClipInstance(*playback.track, clip, player.runtime.diagnostic);
			}
			const bool enabled = clip.enabled && IsTimelineTrackActive(world, owner, player, *state, *playback.track, current);
			const bool marker = playback.track->kind == TimelineTrackKind::Event;
			if (marker) {
				if (enabled && emitEvents && !playback.fired && clip.start <= current && (first || clip.start > previous)) {
					playback.fired = true;
					playback.pendingEvent = true;
				}
				continue;
			}
			const double end = clip.start + clip.duration;
			const bool previewEnd = (mode == TimelineEvaluationMode::Preview || isScrubbing) && current == end && current > clip.start;
			const bool active = enabled && current >= clip.start && (current < end || previewEnd);
			if (playback.track->kind == TimelineTrackKind::Window) {
				if (active) player.runtime.windows.insert(playback.notification.name);
				continue;
			}
			if (playback.track->kind == TimelineTrackKind::Group) continue;
			const bool crossed = enabled && !first && previous < clip.start && current >= end;
			const bool persistent = playback.track->kind == TimelineTrackKind::Motion ||
				(playback.track->kind == TimelineTrackKind::Property && !clip.data.value("restoreOnEnd", true));
			const bool completedSample = enabled && !previewEnd && current >= end &&
				(isScrubbing ? persistent : playback.entered || crossed || first);
			if (!active && !completedSample) {
				ExitTimelineClip(world, owner, state, playback, context, previous, TimelineExitReason::Stopped);
				continue;
			}
			if (playback.target.IsValid() && (!world.IsAlive(playback.target) || world.IsPendingDestroy(playback.target))) {
				ExitTimelineClip(world, owner, state, playback, context, previous, TimelineExitReason::BindingLost);
				continue;
			}
			if (!playback.requiredComponent.empty() &&
				!HasTimelineBindingComponent(world, playback.target, playback.requiredComponent)) {
				ExitTimelineClip(world, owner, state, playback, context, previous, TimelineExitReason::BindingLost);
				player.runtime.diagnostic = playback.track->name + ": 対象に" + playback.requiredComponent + "がありません";
				continue;
			}
			if (playback.configurationChanged) {
				RetireBuiltinTimelineSamples(world, playback.control);
				auto refresh = MakeTimelineEvaluationContext(world, owner, *state, playback, context, current, 0.0);
				configureSampling(refresh);
				refresh.output = &player.runtime;
				refresh.mode = mode == TimelineEvaluationMode::Preview ? mode : TimelineEvaluationMode::Seek;
				std::string diagnostic;
				if (!playback.entered || !playback.instance || !playback.instance->RefreshConfiguration(refresh, clip, diagnostic)) {
					if (!diagnostic.empty()) throw std::invalid_argument(diagnostic);
					ExitTimelineClip(world, owner, state, playback, context, current, TimelineExitReason::Reload);
					playback.instance = MakeTimelineClipInstance(*playback.track, clip, diagnostic);
					if (!diagnostic.empty()) throw std::invalid_argument(diagnostic);
				}
				playback.configurationChanged = false;
			}
			if (!playback.instance && active)
				playback.instance = MakeTimelineClipInstance(*playback.track, clip, player.runtime.diagnostic);
			if (!playback.instance) continue;
			if (boundaryPreparing && !playback.entered) continue;
			if (!playback.entered) {
				CommitBuiltinTimelineSamples(world, context);
				auto entry = MakeTimelineEvaluationContext(world, owner, *state, playback, context, clip.start, 0.0);
				configureSampling(entry);
				entry.mode = evaluationMode;
				entry.output = &player.runtime;
				std::string diagnostic;
				if (!playback.instance->Enter(entry, diagnostic)) {
					playback.instance->Exit(entry, TimelineExitReason::Faulted);
					ReleaseTimelineControls(world, playback.control);
					player.runtime.diagnostic = playback.track->name + ": " + diagnostic;
					continue;
				}
				playback.entered = true;
			}
			auto evaluation = MakeTimelineEvaluationContext(world, owner, *state, playback, context,
				completedSample ? end : current, delta);
			configureSampling(evaluation);
			evaluation.mode = evaluationMode;
			evaluation.output = &player.runtime;
			playback.instance->Evaluate(evaluation);
			if (completedSample) {
				if (playback.instance->GetPreparationProgress() >= 1.0f)
					ExitTimelineClip(world, owner, state, playback, context, end, TimelineExitReason::Completed);
				else boundaryPreparing = true;
			}
			if (context.IsUpdateInterrupted()) break;
		}
	} catch (const std::exception& error) {
		player.runtime.diagnostic = error.what();
		ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::Faulted);
		return false;
	}
	state->evaluatedTime = current;
	state->evaluated = true;
	player.runtime.time = current;
	player.runtime.preparationProgress = 1.0f;
	for (const auto& playback : state->clips) {
		if (playback.entered && playback.instance) player.runtime.preparationProgress =
			(std::min)(player.runtime.preparationProgress, playback.instance->GetPreparationProgress());
	}
	player.runtime.preparing = player.runtime.preparationProgress < 1.0f;
	if (player.runtime.preparing) {
		ApplyTimelineCollisionGates(world, *state);
		player.runtime.windows.clear();
	} else {
		const auto notificationStart = player.runtime.notifications.size();
		for (auto& notification : player.runtime.pendingNotifications) {
			if (player.runtime.notifications.size() >= 4096) break;
			player.runtime.notifications.push_back(std::move(notification));
		}
		player.runtime.pendingNotifications.clear();
		for (auto& playback : state->clips) {
			if (!playback.pendingEvent) continue;
			Emit(player, playback, playback.clip->start);
			playback.pendingEvent = false;
		}
		std::stable_sort(player.runtime.notifications.begin() + notificationStart, player.runtime.notifications.end(),
			[](const auto& first, const auto& second) { return first.time < second.time; });
	}
	return true;
}

}

bool Engine::EvaluateTimelineAt(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	const TimelineAsset& asset, double time, SystemContext& context, TimelineEvaluationMode mode, bool emitEvents, bool isScrubbing) {

	if (!std::isfinite(time) || !world.IsAlive(owner) || world.IsPendingDestroy(owner)) return false;
	auto state = player.runtime.playback;
	const double duration = state && state->source == &asset ? state->asset->duration : GetTimelineAssetDuration(asset);
	const double target = std::abs(time - duration) < 1e-9 ? duration : std::clamp(time, 0.0, duration);
	if (isScrubbing) return EvaluateTimelineSample(world, owner, player, asset, target, context, mode, false, true);
	bool sampled = false;
	if (state && state->source != &asset && state->asset->guid == asset.guid &&
		(state->mode == TimelineEvaluationMode::Preview) == (mode == TimelineEvaluationMode::Preview)) {
		if (!RefreshTimelinePlayback(world, owner, player, asset, context)) return false;
		if (!EvaluateTimelineSample(world, owner, player, asset, state->evaluatedTime, context, mode, false, false)) return false;
		sampled = true;
	}
	if (state && player.runtime.preparing && state->source == &asset && target >= state->evaluatedTime &&
		(state->mode == TimelineEvaluationMode::Preview) == (mode == TimelineEvaluationMode::Preview)) {
		if (!EvaluateTimelineSample(world, owner, player, asset, state->evaluatedTime, context, mode, emitEvents, false)) return false;
		sampled = true;
		if (player.runtime.preparing) return true;
	}
	if (!state || state->source != &asset || target < state->evaluatedTime ||
		(state->mode == TimelineEvaluationMode::Preview) != (mode == TimelineEvaluationMode::Preview)) {
		if (state) ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::Seek);
		if (!EvaluateTimelineSample(world, owner, player, asset, 0.0, context, mode, emitEvents, false)) return false;
		state = player.runtime.playback;
		sampled = true;
	} else if (!state->evaluated) {
		if (!EvaluateTimelineSample(world, owner, player, asset, 0.0, context, mode, emitEvents, false)) return false;
		sampled = true;
	}
	if (player.runtime.preparing) return true;
	while (state && state->evaluatedTime < target) {
		const auto boundary = std::upper_bound(state->boundaries.begin(), state->boundaries.end(), state->evaluatedTime + 1e-10);
		const double sampleTime = boundary != state->boundaries.end() && *boundary < target ? *boundary : target;
		if (!EvaluateTimelineSample(world, owner, player, asset, sampleTime, context, mode, emitEvents, false)) return false;
		sampled = true;
		if (player.runtime.preparing) return true;
		if (context.IsUpdateInterrupted()) return true;
	}
	if (!sampled && state && state->evaluatedTime == target)
		return EvaluateTimelineSample(world, owner, player, asset, target, context, mode, emitEvents, false);
	return state != nullptr;
}

void Engine::AdvanceTimelinePlayer(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	const TimelineAsset& asset, double deltaTime, SystemContext& context) {

	if (!std::isfinite(deltaTime) || deltaTime < 0.0 || !player.runtime.playing || player.runtime.paused) return;
	if (!player.runtime.playback && !PrepareTimelinePlayback(world, owner, player, asset, context,
		player.runtime.edit ? TimelineEvaluationMode::Preview : TimelineEvaluationMode::Playback)) {
		player.runtime.playing = false;
		return;
	}
	auto state = player.runtime.playback;
	if (state->delayRemaining > 0.0) {
		const double consumed = (std::min)(deltaTime, state->delayRemaining);
		state->delayRemaining -= consumed;
		deltaTime -= consumed;
		if (state->delayRemaining > 0.0) return;
	}
	const auto mode = player.runtime.edit ? TimelineEvaluationMode::Preview : TimelineEvaluationMode::Playback;
	const double duration = state->source == &asset ? state->asset->duration : GetTimelineAssetDuration(asset);
	for (uint32_t iteration = 0; iteration < 1024; ++iteration) {
		const double remaining = (std::max)(0.0, duration - player.runtime.time);
		const double step = (std::min)(deltaTime, remaining);
		if (!EvaluateTimelineAt(world, owner, player, asset, player.runtime.time + step, context, mode,
			mode == TimelineEvaluationMode::Playback)) { player.runtime.playing = false; return; }
		deltaTime -= step;
		if (player.runtime.preparing) return;
		if (player.runtime.time < duration) return;
		if (!player.settings.loop) {
			ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::Completed);
			player.runtime.time = duration;
			player.runtime.finished = true;
			return;
		}
		ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::Completed);
		player.runtime.playing = true;
		player.runtime.time = 0.0;
		++player.runtime.cycles;
		if (!PrepareTimelinePlayback(world, owner, player, asset, context, mode)) return;
		player.runtime.playback->delayRemaining = 0.0;
		if (deltaTime <= 0.0) return;
	}
	player.runtime.playing = false;
	player.runtime.diagnostic = "1回の更新で処理できる周回数を超えました";
}
