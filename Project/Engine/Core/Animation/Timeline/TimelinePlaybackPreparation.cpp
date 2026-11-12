#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlayerAccess.h"
#include "TimelineCollisionGate.h"
#include "TimelineBuiltinConfig.h"
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyCatalog.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	struct PlaybackOrder {

		uint64_t next = 0;
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::MakeTimelineClipInstance(const TimelineTrack& track,
	const TimelineClip& clip, std::string& diagnostic) {

	diagnostic.clear();
	try {
		if (track.kind == TimelineTrackKind::Custom) return CreateTimelineCustomClip(track, clip, diagnostic);
		if (track.kind == TimelineTrackKind::SubTimeline) return CreateTimelineSubTimelineClip(clip);
		if (track.kind == TimelineTrackKind::Group || track.kind == TimelineTrackKind::Event ||
			track.kind == TimelineTrackKind::Window) return {};
		return CreateBuiltinTimelineClip(track, clip, diagnostic);
	} catch (const std::exception& error) { diagnostic = error.what(); return {}; }
}

bool Engine::InitializeTimelineClipPlayback(const TimelineTrack& track, const TimelineClip& clip,
	TimelineClipPlayback& playback, std::string& diagnostic) {

	playback.track = &track;
	playback.clip = &clip;
	if (track.kind == TimelineTrackKind::Event || track.kind == TimelineTrackKind::Window) {
		try {
			playback.notification.name = clip.data.value("name", clip.name);
			if (track.kind == TimelineTrackKind::Event) {
				playback.notification.floatParam = clip.data.value("floatParam", 0.0f);
				playback.notification.intParam = clip.data.value("intParam", 0);
				playback.notification.stringParam = clip.data.value("stringParam", std::string{});
				if (!std::isfinite(playback.notification.floatParam))
					throw std::invalid_argument("通知の数値が不正です");
			}
		} catch (const std::exception& error) { diagnostic = error.what(); return false; }
	}
	if (track.kind != TimelineTrackKind::TimeScale && track.kind != TimelineTrackKind::Audio)
		playback.requiredComponent = GetTimelineTrackRequiredComponent(track.kind);
	playback.instance = MakeTimelineClipInstance(track, clip, diagnostic);
	return diagnostic.empty();
}

void Engine::RebuildTimelineBoundaries(TimelinePlaybackState& state) {

	state.boundaries.clear();
	for (const auto& track : state.asset->tracks) {
		if (track.kind == TimelineTrackKind::Group) {
			state.boundaries.push_back(track.start);
			if (track.duration > 0.0) state.boundaries.push_back(track.start + track.duration);
		}
		for (const auto& clip : track.clips) {
			state.boundaries.push_back(clip.start);
			state.boundaries.push_back(clip.start + clip.duration);
		}
	}
	std::sort(state.boundaries.begin(), state.boundaries.end());
	state.boundaries.erase(std::unique(state.boundaries.begin(), state.boundaries.end()), state.boundaries.end());
}

Entity TimelineEvaluationContext::ResolveBinding(UUID id) const {

	for (const auto& binding : bindings) {
		if (binding.id == id) return world.IsAlive(binding.entity) && !world.IsPendingDestroy(binding.entity) ?
			binding.entity : Entity::Null();
	}
	return Entity::Null();
}

Entity TimelineEvaluationContext::ResolveBinding(std::string_view name) const {

	if (const auto id = TryParseUUID16Hex(name)) return ResolveBinding(*id);
	for (const auto& binding : bindings) if (binding.name == name) return ResolveBinding(binding.id);
	return Entity::Null();
}

const TimelineParameterValue* TimelineEvaluationContext::GetParameter(std::string_view name) const {

	const auto id = FindTimelineParameter(asset, name);
	if (playerSettings) return GetTimelineParameterValue(asset, *playerSettings, id);
	for (const auto& parameter : asset.parameters) if (parameter.id == id) return &parameter.value;
	return nullptr;
}

TimelineEvaluationContext Engine::MakeTimelineEvaluationContext(ECSWorld& world, Entity owner,
	TimelinePlaybackState& state, TimelineClipPlayback& playback, SystemContext& context,
	double time, double deltaTime, const TimelineAsset* definition) {

	const auto& clip = *playback.clip;
	const double elapsed = std::clamp(time - clip.start, 0.0, clip.duration);
	const float normalized = clip.duration > 0.0 ? static_cast<float>(elapsed / clip.duration) : 1.0f;
	float weight = 1.0f;
	if (clip.blendIn > 0.0) weight = (std::min)(weight, static_cast<float>(elapsed / clip.blendIn));
	if (clip.blendOut > 0.0) weight = (std::min)(weight, static_cast<float>((clip.duration - elapsed) / clip.blendOut));
	float playbackRate = 1.0f;
	if (state.settings) {
		const auto local = ResolveLocalTime(world, owner, state.settings->timeSource, TimeChannel::Timeline, 1.0f, 1.0f);
		float global = 1.0f;
		if (state.mode != TimelineEvaluationMode::Preview && state.settings->timeSource.mode != TimeSourceMode::Unscaled &&
			context.unscaledDeltaTime > 0.0f) global = (std::max)(context.deltaTime / context.unscaledDeltaTime, 0.0f);
		playbackRate = local.deltaTime * global * state.settings->speed * static_cast<float>(clip.speed);
	}
	if (state.paused) playbackRate = 0.0f;
	TimelineEvaluationContext result{world, context, owner, playback.target, definition ? *definition : *state.asset, *playback.track, clip, state.bindings,
		playback.control, state.mode, time, clip.clipIn + elapsed * clip.speed, (std::min)(deltaTime, elapsed) * clip.speed,
		clip.duration, normalized, std::clamp(weight, 0.0f, 1.0f), state.paused, state.settings, playbackRate};
	result.isScrubbing = state.isScrubbing;
	result.samplingRevision = state.samplingRevision;
	result.playbackState = &state;
	if (state.isScrubbing) { result.deltaTime = 0.0; result.playbackRate = 0.0f; result.paused = true; }
	return result;
}

bool Engine::PrepareTimelinePlayback(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	const TimelineAsset& asset, SystemContext& context, TimelineEvaluationMode mode) {

	std::string diagnostic;
	if (!ValidateTimelineAsset(asset, diagnostic)) {
		player.runtime.diagnostic = std::move(diagnostic);
		return false;
	}
	SetAnimationMaterialContext(world, context.assetDatabase, context.renderAssetLibrary);
	auto state = std::make_shared<TimelinePlaybackState>();
	if (context.assetDatabase && asset.guid) {
		auto cached = GetTimelineAsset(world, *context.assetDatabase, asset.guid, diagnostic);
		if (cached.get() == &asset) state->asset = std::move(cached);
	}
	if (!state->asset) {
		auto definition = std::make_shared<TimelineAsset>(asset);
		UpdateTimelineAssetDuration(*definition);
		state->asset = std::move(definition);
	}
	state->source = &asset;
	state->mode = mode;
	state->lastContext = context;
	state->settings = &player.settings;
	state->lastContext.updateInterruption = {};
	state->order = player.inheritedOrder ? player.inheritedOrder : ++world.GetStorage().Get<PlaybackOrder>().next;
	state->delayRemaining = player.settings.startDelay;
	if (!player.owner) player.owner = Engine::UUID::New();
	for (const auto& slot : state->asset->bindings) {
		auto entity = ResolveTimelineBinding(world, owner, player.settings, slot.id);
		const bool configured = std::any_of(player.settings.bindings.begin(), player.settings.bindings.end(),
			[&](const auto& binding) { return binding.slot == slot.id; });
		if (!configured && slot.name == "Actor") entity = owner;
		state->bindings.push_back({slot.id, slot.name, entity});
	}
	for (const auto& track : state->asset->tracks) {
		for (const auto& clip : track.clips) {
			TimelineClipPlayback playback;
			if (!InitializeTimelineClipPlayback(track, clip, playback, diagnostic)) {
				player.runtime.diagnostic = track.name + ": " + diagnostic;
				return false;
			}
			playback.control = {player.owner, Engine::UUID::New(), player.settings.priority, state->order};
			for (const auto& binding : state->bindings) if (binding.id == track.binding) playback.target = binding.entity;
			state->clips.push_back(std::move(playback));
		}
	}
	RebuildTimelineBoundaries(*state);
	CaptureTimelinePreviewPoses(world, *state);
	std::stable_sort(state->clips.begin(), state->clips.end(), [](const auto& first, const auto& second) {
		return first.clip->start < second.clip->start;
	});
	player.runtime.duration = GetTimelineAssetDuration(asset);
	InitializeTimelineCollisionGates(world, owner, player, *state);
	player.runtime.playback = std::move(state);
	player.runtime.edit = mode == TimelineEvaluationMode::Preview;
	player.runtime.diagnostic.clear();
	return true;
}
