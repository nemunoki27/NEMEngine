#include "TimelinePlayerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlaybackState.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingStorageCleanup.h>

// c++
#include <stdexcept>

using namespace Engine;

namespace {

	// 複製されたハンドルに独立した保存領域を割り当てる
	TimelinePlayerRecord& EnsurePlayer(ECSWorld& world, Entity entity, TimelinePlayerComponent& component) {

		auto& storage = world.GetStorage().Get<TimelinePlayerStorage>();
		auto* record = storage.TryGet(component.handle);
		const auto owner = world.GetUUID(entity);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(TimelinePlayerRecord{
			record ? record->settings : TimelinePlayerSettings{}, {}, owner});
		return *storage.TryGet(component.handle);
	}

	// 動的に割り当てた参照を保存設定の再適用で消さない
	void PreserveRuntimeBindings(const TimelinePlayerSettings& previous, TimelinePlayerSettings& next) {

		PreserveTimeSourceRuntime(previous.timeSource, next.timeSource);
		for (auto& binding : next.bindings) {
			for (const auto& old : previous.bindings) {
				if (binding.slot == old.slot && binding.target.sourceAsset == old.target.sourceAsset &&
					binding.target.localFileID == old.target.localFileID && binding.target.prefab == old.target.prefab)
					binding.runtimeTargetID = old.runtimeTargetID;
			}
		}
	}
}

void TimelinePlayerComponent::InitializeStorage(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component) {

	EnsurePlayer(world, entity, component);
}

void TimelinePlayerComponent::OnAdded(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component) {

	InitializeStorage(world, entity, component);
}

void TimelinePlayerComponent::OnRemoved(ECSWorld& world, [[maybe_unused]] const Entity& entity) {

	FlushTimelineCleanup(world);
}

void TimelinePlayerComponent::ReleaseStorage(ECSWorld& world, const Entity& entity, TimelinePlayerComponent& component) {

	auto& storage = world.GetStorage().Get<TimelinePlayerStorage>();
	auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(entity)) {
		if (record->runtime.playback && world.GetLifetime()->IsAlive()) QueueTimelineCleanup(world, entity, std::move(*record));
		storage.Release(component.handle);
	}
	component.handle = {};
}

void TimelinePlayerComponent::DeserializeECS(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, TimelinePlayerComponent& component) {

	auto candidate = in.get<TimelinePlayerSettings>();
	auto& record = EnsurePlayer(world, entity, component);
	PreserveRuntimeBindings(record.settings, candidate);
	record.settings = std::move(candidate);
}

void TimelinePlayerComponent::SerializeECS(const ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const TimelinePlayerComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<TimelinePlayerStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	if (!record) throw std::logic_error("TimelinePlayerの保存データが見つかりません");
	to_json(out, record->settings);
}

void TimelinePlayerComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& entity,
	const TimelinePlayerComponent& source, TimelinePlayerComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<TimelinePlayerStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<TimelinePlayerStorage>().Emplace(
		TimelinePlayerRecord{record ? record->settings : TimelinePlayerSettings{}, {}, destinationWorld.GetUUID(entity)});
}

const TimelinePlayerRecord* Engine::TryGetTimelinePlayer(const ECSWorld& world, Entity entity) {

	const auto* component = world.TryGetComponentForBinding<TimelinePlayerComponent>(entity);
	const auto* storage = world.GetStorage().TryGet<TimelinePlayerStorage>();
	return component && storage ? storage->TryGet(component->handle) : nullptr;
}

TimelinePlayerRecord* Engine::TryGetTimelinePlayer(ECSWorld& world, Entity entity) {

	auto* component = world.TryGetComponentForBinding<TimelinePlayerComponent>(entity);
	if (!component) return nullptr;
	auto& record = EnsurePlayer(world, entity, *component);
	TrackPendingStorage<TimelinePlayerComponent, TimelinePlayerStorage>(world, entity, &TimelinePlayerComponent::handle);
	return &record;
}

void Engine::SetTimelinePlayerSettings(ECSWorld& world, Entity entity, const TimelinePlayerSettings& settings) {

	TimelinePlayerSettings candidate = nlohmann::json(settings).get<TimelinePlayerSettings>();
	if (auto* record = TryGetTimelinePlayer(world, entity)) {
		PreserveRuntimeBindings(record->settings, candidate);
		record->settings = std::move(candidate);
		world.MarkComponentModified<TimelinePlayerComponent>(entity);
	}
}
