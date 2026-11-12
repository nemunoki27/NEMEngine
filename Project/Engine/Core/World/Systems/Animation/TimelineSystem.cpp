#include "TimelineSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlaybackState.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinEvaluation.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <utility>

using namespace Engine;

namespace {

	struct PlayerEntry {

		Entity entity{};
		TimelinePlayerStorage::Handle handle{};
		int32_t priority = 0;
		uint64_t order = 0;
	};
	struct PlayerScratch {

		std::vector<PlayerEntry> players;
	};

	// 優先度の高いPlayerから同じフレームの制御を確定する
	std::vector<PlayerEntry>& CollectPlayers(ECSWorld& world) {

		auto& players = world.GetStorage().Get<PlayerScratch>().players;
		players.clear();
		world.ForEach<TimelinePlayerComponent>([&](Entity entity, TimelinePlayerComponent& component) {
			if (auto* player = TryGetTimelinePlayer(world, entity)) players.push_back({entity, component.handle,
				player->settings.priority, player->runtime.playback ? player->runtime.playback->order : 0});
		});
		std::stable_sort(players.begin(), players.end(), [](const auto& first, const auto& second) {
			return first.priority != second.priority ? first.priority > second.priority : first.order > second.order;
		});
		return players;
	}

	// 停止やシークは時間倍率が0でも処理する
	bool ProcessCommands(ECSWorld& world, Entity entity, TimelinePlayerRecord& player,
		const TimelineAsset& asset, SystemContext& context) {

		const int32_t request = std::exchange(player.runtime.request, 0);
		const bool edit = context.mode == WorldMode::Edit;
		const auto mode = edit ? TimelineEvaluationMode::Preview : TimelineEvaluationMode::Playback;
		if (!player.runtime.initialized || request == 1 || request == 5) {
			ReleaseTimelinePlayer(world, entity, player, context, request == 5 ? TimelineExitReason::Reset : TimelineExitReason::Stopped);
			player.runtime.initialized = true;
			player.runtime.time = 0.0;
			player.runtime.cycles = 0;
			player.runtime.finished = false;
			player.runtime.paused = false;
			player.runtime.edit = edit;
			player.runtime.playing = request == 1 || (request != 5 && (player.settings.playOnStart || edit));
			if (player.runtime.playing && !EvaluateTimelineAt(world, entity, player, asset, 0.0, context, mode, !edit))
				player.runtime.playing = false;
		}
		if (request == 2) player.runtime.paused = true;
		if (request == 3) {
			player.runtime.paused = false;
			player.runtime.playing = !player.runtime.finished;
			if (player.runtime.playback) player.runtime.playback->isScrubbing = false;
		}
		if (request == 4) ReleaseTimelinePlayer(world, entity, player, context);
		if (request == 6) {
			const bool playing = player.runtime.playing;
			player.runtime.paused = true;
			const bool evaluated = EvaluateTimelineAt(world, entity, player, asset, player.runtime.seekTime, context,
				edit ? TimelineEvaluationMode::Preview : TimelineEvaluationMode::Seek, false, true);
			player.runtime.playing = evaluated && playing;
			player.runtime.paused = true;
			player.runtime.finished = false;
		}
		return player.runtime.playing && !player.runtime.paused;
	}

	// 稼働していないPlayerが残した制御を解放する
	bool CanRun(ECSWorld& world, Entity entity, TimelinePlayerRecord& player, SystemContext& context) {

		if (player.settings.enabled && IsEntityActiveInHierarchy(world, entity) &&
			(context.mode == WorldMode::Play || player.settings.playInEditMode)) return true;
		if (player.runtime.playback) ReleaseTimelinePlayer(world, entity, player, context);
		return false;
	}
}

void TimelineSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play || !context.assetDatabase) return;
	FlushTimelineCleanup(world);
	const auto& entries = CollectPlayers(world);
	for (const auto& entry : entries) {
		if (context.IsUpdateInterrupted()) break;
		auto* player = world.GetStorage().Get<TimelinePlayerStorage>().TryGet(entry.handle);
		if (!player || !CanRun(world, entry.entity, *player, context) || player->settings.timeSource.mode == TimeSourceMode::Unscaled)
			continue;
		std::string diagnostic;
		const auto asset = GetTimelineAsset(world, *context.assetDatabase, player->settings.timeline, diagnostic);
		if (!asset) {
			ReleaseTimelinePlayer(world, entry.entity, *player, context, TimelineExitReason::Reload);
			player->runtime.diagnostic = std::move(diagnostic);
			player->runtime.initialized = false;
			continue;
		}
		if (!ProcessCommands(world, entry.entity, *player, *asset, context) || player->runtime.preparing) {
			if (player->runtime.playback && (player->runtime.paused || player->runtime.preparing))
				EvaluateTimelineAt(world, entry.entity, *player, *asset, player->runtime.time, context,
					TimelineEvaluationMode::Playback, false, player->runtime.playback->isScrubbing);
			continue;
		}
		const auto time = ResolveLocalTime(world, entry.entity, player->settings.timeSource, TimeChannel::Timeline,
			context.fixedDeltaTime, context.fixedDeltaTime);
		if (time.valid) AdvanceTimelinePlayer(world, entry.entity, *player, *asset,
			time.deltaTime * player->settings.speed, context);
		else player->runtime.diagnostic = "時間源が無効です";
	}
}

void TimelineAttachmentSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	UpdateTimelineAttachmentsForPhysics(world, context);
}

void TimelineSystem::Update(ECSWorld& world, SystemContext& context) {

	FlushTimelineCleanup(world);
	UpdateBuiltinTimelineRealtime(world, context.unscaledDeltaTime);
	if (!context.assetDatabase) return;
	const auto& entries = CollectPlayers(world);
	for (const auto& entry : entries) {
		if (context.IsUpdateInterrupted()) break;
		auto* player = world.GetStorage().Get<TimelinePlayerStorage>().TryGet(entry.handle);
		if (!player || !CanRun(world, entry.entity, *player, context)) continue;
		std::string diagnostic;
		const auto asset = GetTimelineAsset(world, *context.assetDatabase, player->settings.timeline, diagnostic);
		if (!asset) {
			ReleaseTimelinePlayer(world, entry.entity, *player, context, TimelineExitReason::Reload);
			player->runtime.diagnostic = std::move(diagnostic);
			player->runtime.initialized = false;
			continue;
		}
		if (!ProcessCommands(world, entry.entity, *player, *asset, context) || player->runtime.preparing) {
			if (player->runtime.playback && (player->runtime.paused || player->runtime.preparing))
				EvaluateTimelineAt(world, entry.entity, *player, *asset, player->runtime.time, context,
					context.mode == WorldMode::Edit ? TimelineEvaluationMode::Preview : TimelineEvaluationMode::Playback, false,
					player->runtime.playback->isScrubbing);
			continue;
		}
		if (context.mode != WorldMode::Edit && player->settings.timeSource.mode != TimeSourceMode::Unscaled) {
			if (context.deltaTime <= 0.0f) EvaluateTimelineAt(world, entry.entity, *player, *asset, player->runtime.time,
				context, TimelineEvaluationMode::Playback, false);
			continue;
		}
		const auto time = ResolveLocalTime(world, entry.entity, player->settings.timeSource, TimeChannel::Timeline,
			context.mode == WorldMode::Edit ? context.unscaledDeltaTime : context.deltaTime, context.unscaledDeltaTime);
		if (time.valid) AdvanceTimelinePlayer(world, entry.entity, *player, *asset,
			time.deltaTime * player->settings.speed, context);
	}
}

void TimelineSystem::OnWorldExit(ECSWorld& world, SystemContext& context) {

	FlushTimelineCleanup(world);
	world.ForEach<TimelinePlayerComponent>([&](Entity entity, TimelinePlayerComponent&) {
		if (auto* player = TryGetTimelinePlayer(world, entity))
			ReleaseTimelinePlayer(world, entity, *player, context, TimelineExitReason::WorldExit);
	});
}
