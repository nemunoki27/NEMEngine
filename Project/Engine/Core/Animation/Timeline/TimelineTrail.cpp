#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <optional>

using namespace Engine;

namespace {

	struct TrailBaseline {

		size_t references = 0;
		bool emitting = false;
		std::optional<TrailRendererRuntime> preview;
	};
	struct TrailBaselines {

		std::unordered_map<uint64_t, TrailBaseline> values;
	};

	uint64_t EntityKey(Entity entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	void ReleaseGate(ECSWorld& world, const TimelineTrailGate& gate) {

		auto& values = world.GetStorage().Get<TrailBaselines>().values;
		auto found = values.find(EntityKey(gate.target));
		if (found != values.end() && --found->second.references == 0) {
			if (auto* trail = TryGetTrailRenderer(world, gate.target);
				trail && CanRestoreTimelineControl(world, gate.target, "TrailRenderer", "emitting", gate.control)) {
				SetTrailEmitting(world, gate.target, found->second.emitting);
				if (found->second.preview) trail->runtime = std::move(*found->second.preview);
				else {
					trail->runtime.timelineHoldsHistory = false;
					BreakTrail(world, gate.target);
				}
			}
			values.erase(found);
		}
		ReleaseTimelineControls(world, gate.control);
	}

	// クリップの境界もサンプリングし、短い発生区間と区間同士の切れ目を保つ
	void SampleHistory(ECSWorld& world, Entity owner, TimelinePlayerRecord& player, TimelinePlaybackState& state,
		SystemContext& context, Entity target, TrailRendererRecord& trail, double time,
		const std::vector<const TimelineClipPlayback*>& clips) {

		const double begin = (std::max)(0.0, time - trail.settings.lifetime);
		const double step = (std::max)(1.0 / std::clamp(state.asset->frameRate, 1.0, 240.0), (time - begin) / 2048.0);
		std::vector<double> samples{begin, time};
		const size_t count = static_cast<size_t>(std::ceil((time - begin) / step));
		for (size_t i = 1; i < count; ++i) samples.push_back(begin + i * step);
		for (const auto boundary : state.boundaries)
			if (begin < boundary && boundary < time) samples.push_back(boundary);
		std::sort(samples.begin(), samples.end());
		samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
		const bool blade = trail.settings.mode == TrailMode::TwoPoints;
		const Entity root = !blade ? target : trail.runtime.rootOverride.IsValid() ? trail.runtime.rootOverride : trail.settings.root.Resolve(world, target);
		const Entity tip = !blade ? target : trail.runtime.tipOverride.IsValid() ? trail.runtime.tipOverride : trail.settings.tip.Resolve(world, target);
		ClearTrail(world, target);
		trail.runtime.time = begin;
		if (!IsEntityActiveInHierarchy(world, target) || !IsEntityActiveInHierarchy(world, root) || !IsEntityActiveInHierarchy(world, tip) ||
			world.IsPendingDestroy(root) || world.IsPendingDestroy(tip)) { trail.runtime.time = time; return; }
		auto groups = state.groups;
		auto branches = state.branches;
		ScopedCleanup restore([&]() noexcept { state.groups = std::move(groups); state.branches = std::move(branches); });
		double previous = begin;
		for (const double at : samples) {
			AgeTrail(trail, at - previous);
			previous = at;
			bool active = false, endsHere = false;
			for (const auto* item : clips) {
				const auto& clip = *item->clip;
				const double end = clip.start + clip.duration;
				if (at < clip.start || end < at || !IsTimelineTrackActive(world, owner, player, state, *item->track, at)) continue;
				if (at < end) active = true;
				else endsHere = true;
			}
			Matrix4x4 a{}, b{};
			if ((active || endsHere) && world.IsAlive(root) && world.IsAlive(tip) &&
				SampleTimelineTargetTransform(world, owner, player, state, context, at, root, a) &&
				(!blade || SampleTimelineTargetTransform(world, owner, player, state, context, at, tip, b))) {
				const auto normal = Vector3::NormalizeOr({a.m[2][0], a.m[2][1], a.m[2][2]}, {0, 0, 1});
				const auto position = a.GetTranslationValue();
				AppendTrailSample(trail, position, blade ? b.GetTranslationValue() : position, normal);
			} else BreakTrail(world, target);
			if (!active) BreakTrail(world, target);
		}
	}
}

void Engine::EvaluateTimelineTrails(ECSWorld& world, Entity owner, TimelinePlayerRecord& player, TimelinePlaybackState& state,
	SystemContext& context, double time, TimelineEvaluationMode mode) {

	std::unordered_map<uint64_t, std::vector<const TimelineClipPlayback*>> sources;
	for (const auto& item : state.clips) {
		if (item.track->kind != TimelineTrackKind::Trail || !item.clip->enabled || !world.IsAlive(item.target) ||
			world.IsPendingDestroy(item.target) || IsTimelineTrackMuted(*state.asset, *item.track)) continue;
		auto* trail = TryGetTrailRenderer(world, item.target);
		if (!trail) continue;
		const auto key = EntityKey(item.target);
		sources[key].push_back(&item);
		if (std::any_of(state.trailGates.begin(), state.trailGates.end(), [&](const auto& gate) { return gate.target == item.target; })) continue;
		auto& baseline = world.GetStorage().Get<TrailBaselines>().values[key];
		if (!baseline.references++) {
			baseline.emitting = trail->settings.emitting;
			if (state.mode == TimelineEvaluationMode::Preview) baseline.preview = trail->runtime;
		}
		auto control = item.control;
		control.instance = Engine::UUID::New();
		AcquireTimelineControl(world, item.target, "TrailRenderer", "emitting", control);
		state.trailGates.push_back({item.target, control});
	}
	std::erase_if(state.trailGates, [&](const auto& gate) {
		const auto found = sources.find(EntityKey(gate.target));
		if (found == sources.end()) { ReleaseGate(world, gate); return true; }
		if (!OwnsTimelineControl(world, gate.target, "TrailRenderer", "emitting", gate.control)) return false;
		auto& trail = *TryGetTrailRenderer(world, gate.target);
		const bool active = !player.runtime.preparing && std::any_of(found->second.begin(), found->second.end(), [&](const auto* item) {
			return item->clip->start <= time && time < item->clip->start + item->clip->duration &&
				IsTimelineTrackActive(world, owner, player, state, *item->track, time);
		});
		trail.runtime.timelineHoldsHistory = state.isScrubbing || mode != TimelineEvaluationMode::Playback || state.paused || player.runtime.preparing;
		SetTrailEmitting(world, gate.target, active);
		if (!player.runtime.preparing && (state.isScrubbing || mode != TimelineEvaluationMode::Playback))
			SampleHistory(world, owner, player, state, context, gate.target, trail, time, found->second);
		return false;
	});
}

void Engine::ReleaseTimelineTrails(ECSWorld& world, TimelinePlaybackState& state) {

	for (const auto& gate : state.trailGates) ReleaseGate(world, gate);
	state.trailGates.clear();
}
