#include "TimelineCollisionGate.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlaybackState.h"
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <unordered_map>

using namespace Engine;

namespace {

	struct CollisionGateBaseline {

		uint32_t references = 0;
		bool enabled = true;
		bool previewOnly = true;
	};
	struct CollisionGateStorage {

		std::unordered_map<uint64_t, CollisionGateBaseline> baselines;
	};

	uint64_t EntityKey(Entity entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	// 非表示の親グループに含まれる判定は操作しない
	bool IsMuted(const TimelineAsset& asset, const TimelineTrack& track) {

		const auto* current = &track;
		for (size_t i = 0; current && i <= asset.tracks.size(); ++i) {
			if (current->muted) return true;
			if (!current->parent) return false;
			const auto found = std::find_if(asset.tracks.begin(), asset.tracks.end(),
				[&](const auto& parent) { return parent.id == current->parent; });
			current = found == asset.tracks.end() ? nullptr : &*found;
		}
		return current != nullptr;
	}

	void SetEnabled(ECSWorld& world, Entity entity, bool enabled) {

		if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) return;
		auto* component = world.TryGetComponentForBinding<CollisionComponent>(entity);
		if (!component || component->enabled == enabled) return;
		component->enabled = enabled;
		world.MarkComponentModified<CollisionComponent>(entity);
	}

	// 再生中に割り当て先が変わった場合も新しい対象を区間外で無効にする
	void RegisterGates(ECSWorld& world, TimelinePlaybackState& state) {

		if (!state.asset) return;
		auto& baselines = world.GetStorage().Get<CollisionGateStorage>().baselines;
		for (const auto& playback : state.clips) {
			if (!playback.track || !playback.clip || playback.track->kind != TimelineTrackKind::Collision ||
				!playback.clip->enabled || IsMuted(*state.asset, *playback.track)) continue;
			auto entity = playback.target;
			if (playback.track->binding) {
				entity = Entity::Null();
				for (const auto& binding : state.bindings)
					if (binding.id == playback.track->binding) { entity = binding.entity; break; }
			}
			if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) continue;
			const auto* component = world.TryGetComponentForBinding<CollisionComponent>(entity);
			if (!component || std::any_of(state.collisionGates.begin(), state.collisionGates.end(),
				[&](const auto& gate) { return gate.target == entity; })) continue;
			auto& baseline = baselines[EntityKey(entity)];
			if (!baseline.references) baseline.enabled = component->enabled;
			++baseline.references;
			baseline.previewOnly = baseline.previewOnly && state.mode == TimelineEvaluationMode::Preview;
			auto control = playback.control;
			control.instance = Engine::UUID::New();
			AcquireTimelineControl(world, entity, "Collision", "enabled", control);
			state.collisionGates.push_back({entity, control});
		}
	}

	// 編集で削除した判定も通常終了と同じ経路で返す
	void ReleaseGate(ECSWorld& world, const TimelineCollisionGate& gate, bool preview) {

		auto& baselines = world.GetStorage().Get<CollisionGateStorage>().baselines;
		const auto found = baselines.find(EntityKey(gate.target));
		if (found != baselines.end()) {
			auto& baseline = found->second;
			if (baseline.references) --baseline.references;
			const bool restore = !baseline.references && preview && baseline.previewOnly &&
				CanRestoreTimelineControl(world, gate.target, "Collision", "enabled", gate.control);
			if (restore) SetEnabled(world, gate.target, baseline.enabled);
			else if (OwnsTimelineControl(world, gate.target, "Collision", "enabled", gate.control))
				SetEnabled(world, gate.target, false);
			if (!baseline.references) baselines.erase(found);
		}
		ReleaseTimelineControls(world, gate.control);
	}
}

void Engine::InitializeTimelineCollisionGates(ECSWorld& world, [[maybe_unused]] Entity owner,
	[[maybe_unused]] TimelinePlayerRecord& player, TimelinePlaybackState& state) {

	ApplyTimelineCollisionGates(world, state);
}

void Engine::ApplyTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state) {

	RegisterGates(world, state);
	for (const auto& gate : state.collisionGates)
		if (OwnsTimelineControl(world, gate.target, "Collision", "enabled", gate.control))
			SetEnabled(world, gate.target, false);
}

void Engine::ReleaseTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state, bool preview) {

	const auto* storage = world.GetStorage().TryGet<CollisionGateStorage>();
	if (!storage) { state.collisionGates.clear(); return; }
	for (const auto& gate : state.collisionGates) ReleaseGate(world, gate, preview);
	state.collisionGates.clear();
}

void Engine::RefreshTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state) {

	std::erase_if(state.collisionGates, [&](const auto& gate) {
		const bool required = std::any_of(state.clips.begin(), state.clips.end(), [&](const auto& playback) {
			if (playback.track->kind != TimelineTrackKind::Collision || !playback.clip->enabled ||
				IsMuted(*state.asset, *playback.track)) return false;
			for (const auto& binding : state.bindings)
				if (binding.id == playback.track->binding) return binding.entity == gate.target;
			return playback.target == gate.target;
		});
		if (!required) ReleaseGate(world, gate, state.mode == TimelineEvaluationMode::Preview);
		return !required;
	});
	RegisterGates(world, state);
}
