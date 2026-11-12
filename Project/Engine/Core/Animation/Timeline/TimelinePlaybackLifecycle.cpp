#include "TimelinePlaybackState.h"
#include "TimelineBuiltinEvaluation.h"
#include "TimelineCollisionGate.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <exception>
#include <utility>

using namespace Engine;

namespace {

	struct PendingCleanup {

		std::vector<std::pair<Entity, TimelinePlayerRecord>> players;
		bool flushing = false;
	};
}

void Engine::ReleaseTimelinePlayer(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	SystemContext& context, TimelineExitReason reason) {

	auto state = player.runtime.playback;
	if (!state || state->releasing) return;
	state->releasing = true;
	state->settings = nullptr;
	try { CommitBuiltinTimelineSamples(world, context); }
	catch (const std::exception& error) { player.runtime.diagnostic = error.what(); }
	catch (...) { player.runtime.diagnostic = "Timelineの最終評価に失敗しました"; }
	for (auto& clip : state->clips) {
		if (clip.entered && clip.instance) {
			auto evaluation = MakeTimelineEvaluationContext(world, owner, *state, clip, context, state->evaluatedTime, 0.0);
			try { clip.instance->Retire(evaluation); } catch (...) { }
		}
		RetireBuiltinTimelineSamples(world, clip.control);
		const bool entered = clip.entered;
		clip.entered = false;
		auto systems = context;
		systems.updateInterruption = {};
		DeferBuiltinTimelineExit(world, [&, state, pointer = &clip, owner, systems, entered, reason]() mutable {
			if (entered && pointer->instance) {
				auto evaluation = MakeTimelineEvaluationContext(world, owner, *state, *pointer, systems, state->evaluatedTime, 0.0);
				try { pointer->instance->Exit(evaluation, reason); }
				catch (...) { }
			}
			ReleaseTimelineControls(world, pointer->control);
		});
	}
	DeferBuiltinTimelineExit(world, [&, state]() {
		ReleaseTimelineCollisionGates(world, *state, state->mode == TimelineEvaluationMode::Preview);
	});
	if (state->mode == TimelineEvaluationMode::Preview && state->root && reason != TimelineExitReason::Completed) {
		DeferBuiltinTimelineExit(world, [&, state]() {
			for (const auto& pose : state->previewPoses) {
				if (auto* transform = world.TryGetComponent<TransformComponent>(pose.entity)) {
					transform->localPos = pose.position;
					transform->localRotation = pose.rotation;
					transform->localScale = pose.scale;
					MarkTransformSubtreeDirty(world, pose.entity);
				}
			}
		});
	}
	player.runtime.windows.clear();
	player.runtime.pendingNotifications.clear();
	player.runtime.playback.reset();
	player.runtime.playing = false;
	player.runtime.paused = false;
	player.runtime.preparing = false;
	player.runtime.preparationProgress = 1.0f;
}

void Engine::QueueTimelineCleanup(ECSWorld& world, Entity owner, TimelinePlayerRecord&& player) {

	world.GetStorage().Get<PendingCleanup>().players.emplace_back(owner, std::move(player));
}

void Engine::FlushTimelineCleanup(ECSWorld& world) {

	if (!world.GetStorage().TryGet<PendingCleanup>()) return;
	auto& pending = world.GetStorage().Get<PendingCleanup>();
	if (pending.flushing) return;
	pending.flushing = true;
	auto players = std::move(pending.players);
	pending.players.clear();
	for (auto& [owner, player] : players) {
		if (!player.runtime.playback) continue;
		auto context = player.runtime.playback->lastContext;
		context.world = &world;
		context.updateInterruption = {};
		ReleaseTimelinePlayer(world, owner, player, context, TimelineExitReason::BindingLost);
	}
	pending.flushing = false;
}
