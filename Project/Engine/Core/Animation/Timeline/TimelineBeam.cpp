#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <iterator>

using namespace Engine;

namespace {

	struct BeamBaseline {

		size_t references = 0;
		bool initialized = false;
		bool playing = false;
		bool timelineControlled = false;
		double time = 0.0;
	};
	struct BeamBaselines {

		std::unordered_map<uint64_t, BeamBaseline> values;
	};

	uint64_t EntityKey(Entity entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	void ReleaseGate(ECSWorld& world, const TimelineBeamGate& gate) {

		auto& values = world.GetStorage().Get<BeamBaselines>().values;
		auto found = values.find(EntityKey(gate.target));
		if (found != values.end() && --found->second.references == 0) {
			if (auto* beam = TryGetBeamRenderer(world, gate.target);
				beam && CanRestoreTimelineControl(world, gate.target, "BeamRenderer", "playing", gate.control)) {
				const auto& saved = found->second;
				beam->runtime.playing = saved.playing;
				beam->runtime.initialized = saved.initialized;
				beam->runtime.timelineControlled = saved.timelineControlled;
				beam->runtime.time = saved.time;
				UpdateBeamGeometry(world, gate.target, *beam);
			}
			values.erase(found);
		}
		ReleaseTimelineControls(world, gate.control);
	}
}

void Engine::EvaluateTimelineBeams(ECSWorld& world, Entity owner, TimelinePlayerRecord& player, TimelinePlaybackState& state,
	double time, TimelineEvaluationMode mode) {

	for (auto& gate : state.beamGates) {
		gate.bound = false;
		gate.active = nullptr;
	}
	for (const auto& item : state.clips) {
		if (item.track->kind != TimelineTrackKind::Beam || !item.clip->enabled || !world.IsAlive(item.target) ||
			world.IsPendingDestroy(item.target) || IsTimelineTrackMuted(*state.asset, *item.track)) continue;
		auto* beam = TryGetBeamRenderer(world, item.target);
		if (!beam) continue;
		auto gate = std::find_if(state.beamGates.begin(), state.beamGates.end(),
			[&](const auto& current) { return current.target == item.target; });
		if (gate == state.beamGates.end()) {
			auto& saved = world.GetStorage().Get<BeamBaselines>().values[EntityKey(item.target)];
			if (saved.references++ == 0) {
				saved.initialized = beam->runtime.initialized;
				saved.playing = beam->runtime.playing;
				saved.timelineControlled = beam->runtime.timelineControlled;
				saved.time = beam->runtime.time;
			}
			auto control = item.control;
			control.instance = Engine::UUID::New();
			AcquireTimelineControl(world, item.target, "BeamRenderer", "playing", control);
			state.beamGates.push_back({item.target, control});
			gate = std::prev(state.beamGates.end());
		}
		gate->bound = true;
		const auto& clip = *item.clip;
		if (!player.runtime.preparing && clip.start <= time && time < clip.start + clip.duration &&
			(!gate->active || gate->active->start <= clip.start) && IsTimelineTrackActive(world, owner, player, state, *item.track, time))
			gate->active = &clip;
	}
	std::erase_if(state.beamGates, [&](const auto& gate) {
		if (!gate.bound) { ReleaseGate(world, gate); return true; }
		if (!OwnsTimelineControl(world, gate.target, "BeamRenderer", "playing", gate.control)) return false;
		auto& beam = *TryGetBeamRenderer(world, gate.target);
		beam.runtime.initialized = true;
		beam.runtime.timelineControlled = true;
		beam.runtime.playing = gate.active != nullptr;
		beam.runtime.time = gate.active ? gate.active->clipIn + (time - gate.active->start) * gate.active->speed : 0.0;
		if (state.isScrubbing || mode != TimelineEvaluationMode::Playback) {
			DeferBuiltinTimelineExit(world, [&world, target = gate.target]() {
				if (auto* current = TryGetBeamRenderer(world, target)) UpdateBeamGeometry(world, target, *current);
			});
		}
		return false;
	});
}

void Engine::ReleaseTimelineBeams(ECSWorld& world, TimelinePlaybackState& state) {

	for (const auto& gate : state.beamGates) ReleaseGate(world, gate);
	state.beamGates.clear();
}
