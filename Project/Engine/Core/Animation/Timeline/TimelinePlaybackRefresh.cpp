#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlayerAccess.h"
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <limits>
#include <unordered_map>

using namespace Engine;

namespace {

	struct PendingClip {

		TimelineClipPlayback playback;
		size_t previous = (std::numeric_limits<size_t>::max)();
		bool configurationChanged = false;
	};

	// 編集で通知済みの時刻へ戻っても再通知しない
	bool SameConfiguration(const TimelineClip& first, const TimelineClip& second) {

		return first.data == second.data && first.easing == second.easing;
	}

	// 無関係なクリップ編集では開始時に確定した分岐を変えない
	bool SameConditions(const TimelineAsset& first, const TimelineAsset& second) {

		const auto condition = [](const TimelineCondition& a, const TimelineCondition& b) {
			return a.kind == b.kind && a.comparison == b.comparison && a.parameter == b.parameter &&
				a.firstBinding == b.firstBinding && a.secondBinding == b.secondBinding && a.plane == b.plane &&
				a.distance == b.distance && a.value == b.value;
		};
		if (first.parameters.size() != second.parameters.size() || !std::equal(first.parameters.begin(), first.parameters.end(),
			second.parameters.begin(), [](const auto& a, const auto& b) { return a.id == b.id && a.value == b.value; })) return false;
		std::vector<const TimelineTrack*> oldGroups, newGroups;
		const auto collect = [](const auto& tracks, auto& groups) {
			for (const auto& track : tracks)
				if (track.kind == TimelineTrackKind::Group || !track.conditions.empty() || track.branchGroup) groups.push_back(&track);
		};
		collect(first.tracks, oldGroups);
		collect(second.tracks, newGroups);
		return oldGroups.size() == newGroups.size() && std::equal(oldGroups.begin(), oldGroups.end(), newGroups.begin(),
			[&](const auto* a, const auto* b) {
				return a->id == b->id && a->parent == b->parent && a->muted == b->muted && a->branchGroup == b->branchGroup &&
					a->start == b->start && a->duration == b->duration && a->fallback == b->fallback &&
					a->conditionsAll == b->conditionsAll && a->conditions.size() == b->conditions.size() &&
					std::equal(a->conditions.begin(), a->conditions.end(), b->conditions.begin(), condition);
			});
	}

	// 独自クリップが借用する名前と型の追加削除を検出する
	bool SameContextSchema(const TimelineAsset& first, const TimelineAsset& second) {

		return first.bindings.size() == second.bindings.size() && first.parameters.size() == second.parameters.size() &&
			std::equal(first.bindings.begin(), first.bindings.end(), second.bindings.begin(), [](const auto& a, const auto& b) {
				return a.id == b.id && a.name == b.name;
			}) && std::equal(first.parameters.begin(), first.parameters.end(), second.parameters.begin(), [](const auto& a, const auto& b) {
				return a.id == b.id && a.name == b.name && a.value.index() == b.value.index();
			});
	}

	// 参照を差し替える前に新しい対象を解決する
	std::vector<TimelineResolvedBinding> ResolveBindings(ECSWorld& world, Entity owner,
		const TimelinePlayerSettings& settings, const TimelineAsset& asset) {

		std::vector<TimelineResolvedBinding> bindings;
		bindings.reserve(asset.bindings.size());
		for (const auto& slot : asset.bindings) {
			const bool configured = std::any_of(settings.bindings.begin(), settings.bindings.end(),
				[&](const auto& binding) { return binding.slot == slot.id; });
			const auto target = !configured && slot.name == "Actor" ? owner : ResolveTimelineBinding(world, owner, settings, slot.id);
			bindings.push_back({slot.id, slot.name, target});
		}
		return bindings;
	}

}

void Engine::CaptureTimelinePreviewPoses(ECSWorld& world, TimelinePlaybackState& state) {

	for (const auto& binding : state.bindings) {
		if (std::any_of(state.previewPoses.begin(), state.previewPoses.end(),
			[&](const auto& pose) { return pose.entity == binding.entity; })) continue;
		if (const auto* transform = world.TryGetComponentForBinding<TransformComponent>(binding.entity))
			state.previewPoses.push_back({binding.entity, transform->localPos, transform->localScale, transform->localRotation});
	}
}

bool Engine::RefreshTimelinePlayback(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	const TimelineAsset& asset, SystemContext& context) {

	const auto state = player.runtime.playback;
	if (!state || state->source == &asset) return true;
	std::string diagnostic;
	if (!ValidateTimelineAsset(asset, diagnostic)) {
		player.runtime.diagnostic = std::move(diagnostic);
		return false;
	}
	auto definition = std::make_shared<TimelineAsset>(asset);
	UpdateTimelineAssetDuration(*definition);
	auto bindings = ResolveBindings(world, owner, player.settings, *definition);
	const bool sameContextSchema = SameContextSchema(*state->asset, *definition);
	const bool sameConditions = SameConditions(*state->asset, *definition) && state->bindings.size() == bindings.size() &&
		std::equal(state->bindings.begin(), state->bindings.end(), bindings.begin(),
			[](const auto& a, const auto& b) { return a.id == b.id && a.entity == b.entity; });
	std::unordered_map<UUID, size_t> previous;
	previous.reserve(state->clips.size());
	for (size_t i = 0; i < state->clips.size(); ++i) previous.emplace(state->clips[i].clip->id, i);
	std::vector<PendingClip> pending;
	for (const auto& track : definition->tracks) for (const auto& clip : track.clips) {
		PendingClip next;
		next.playback.track = &track;
		next.playback.clip = &clip;
		for (const auto& binding : bindings) if (binding.id == track.binding) next.playback.target = binding.entity;
		const auto found = previous.find(clip.id);
		if (found != previous.end()) {
			const auto& old = state->clips[found->second];
			if (old.track->kind == track.kind && old.target == next.playback.target) {
				next.previous = found->second;
				next.configurationChanged = !SameConfiguration(*old.clip, clip) ||
					(track.kind == TimelineTrackKind::Custom && !sameContextSchema);
				if (!next.configurationChanged) {
					next.playback.notification = old.notification;
					next.playback.requiredComponent = old.requiredComponent;
				}
			}
		}
		if ((next.previous >= state->clips.size() || next.configurationChanged) &&
			!InitializeTimelineClipPlayback(track, clip, next.playback, diagnostic)) {
			player.runtime.diagnostic = track.name + ": " + diagnostic;
			return false;
		}
		pending.push_back(std::move(next));
	}
	std::vector<bool> retained(state->clips.size(), false);
	for (const auto& next : pending) if (next.previous < retained.size()) retained[next.previous] = true;
	for (size_t i = 0; i < state->clips.size(); ++i) {
		if (!retained[i]) ExitTimelineClip(world, owner, state, state->clips[i], context,
			state->evaluatedTime, TimelineExitReason::Reload);
	}
	std::vector<TimelineClipPlayback> clips;
	clips.reserve(pending.size());
	for (auto& next : pending) {
		auto& playback = next.playback;
		if (next.previous < state->clips.size()) {
			auto& old = state->clips[next.previous];
			playback.instance = std::move(old.instance);
			playback.control = old.control;
			playback.entered = old.entered;
			playback.fired = old.fired;
			playback.pendingEvent = old.pendingEvent && !next.configurationChanged;
			playback.configurationChanged = old.configurationChanged || next.configurationChanged;
			if (playback.track->kind == TimelineTrackKind::Event &&
				(next.configurationChanged || old.clip->start != playback.clip->start)) {
				playback.fired = playback.clip->start <= state->evaluatedTime;
				playback.pendingEvent = false;
			}
		} else {
			playback.control = {player.owner, UUID::New(), player.settings.priority, state->order};
			playback.fired = playback.clip->start <= state->evaluatedTime;
		}
		clips.push_back(std::move(playback));
	}
	state->asset = std::move(definition);
	state->source = &asset;
	state->bindings = std::move(bindings);
	state->clips = std::move(clips);
	if (!sameConditions) { state->groups.clear(); state->branches.clear(); }
	state->refreshed = true;
	state->evaluatedTime = (std::min)(state->evaluatedTime, state->asset->duration);
	player.runtime.time = (std::min)(player.runtime.time, state->asset->duration);
	player.runtime.duration = state->asset->duration;
	CaptureTimelinePreviewPoses(world, *state);
	RebuildTimelineBoundaries(*state);
	std::stable_sort(state->clips.begin(), state->clips.end(), [](const auto& first, const auto& second) {
		return first.clip->start < second.clip->start;
	});
	RefreshTimelineCollisionGates(world, *state);
	player.runtime.diagnostic.clear();
	return true;
}
