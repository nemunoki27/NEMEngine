#include "TimelinePlayerAccess.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>

// c++
#include <algorithm>
#include <unordered_map>

using namespace Engine;

namespace {

	struct DefinitionEntry {

		std::shared_ptr<const TimelineAsset> asset;
		std::filesystem::path path;
		uint64_t contentRevision = 0, structureRevision = 0;
		bool attempted = false;
		std::string diagnostic;
	};
	struct DefinitionStorage {

		std::unordered_map<AssetID, DefinitionEntry> assets;
		std::unordered_map<AssetID, std::shared_ptr<const TimelineAsset>> overrides;
	};
}

bool Engine::HasTimelineBindingComponent(const ECSWorld& world, Entity entity, std::string_view component) {

	if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) return false;
	if (component.empty()) return true;
	if (component == "Camera") return world.HasComponent<PerspectiveCameraComponent>(entity) ||
		world.HasComponent<OrthographicCameraComponent>(entity);
	return world.HasComponent(entity, component);
}

std::shared_ptr<const TimelineAsset> Engine::GetTimelineAsset(ECSWorld& world, AssetDatabase& database,
	AssetID id, std::string& diagnostic) {

	if (!id) { diagnostic = "Timelineアセットを設定してください"; return {}; }
	auto& storage = world.GetStorage().Get<DefinitionStorage>();
	if (const auto found = storage.overrides.find(id); found != storage.overrides.end()) {
		diagnostic.clear();
		return found->second;
	}
	auto& entry = storage.assets[id];
	const auto revision = database.GetContentRevision(id);
	const auto structure = database.GetStructureRevision();
	bool reload = !entry.attempted || revision != entry.contentRevision;
	if (entry.structureRevision != structure || !entry.attempted) {
		const auto path = database.ResolveFullPath(id);
		reload |= path != entry.path;
		entry.path = path;
		entry.structureRevision = structure;
	}
	if (reload) {
		entry.attempted = true;
		entry.contentRevision = revision;
		auto candidate = std::make_shared<TimelineAsset>();
		if (LoadTimelineAsset(entry.path, *candidate, &entry.diagnostic)) {
			candidate->guid = id;
			entry.asset = std::move(candidate);
		} else entry.asset.reset();
	}
	diagnostic = entry.diagnostic;
	return entry.asset;
}

bool Engine::PublishTimelineAssetOverride(ECSWorld& world, AssetID id,
	std::shared_ptr<const TimelineAsset> asset, std::string* diagnostic) {

	std::string error;
	if (!id || !asset || (asset->guid && asset->guid != id) || !ValidateTimelineAsset(*asset, error)) {
		if (diagnostic) *diagnostic = error.empty() ? "Timelineの編集データが不正です" : std::move(error);
		return false;
	}
	auto& overrides = world.GetStorage().Get<DefinitionStorage>().overrides;
	if (asset->guid == id && asset->duration == GetTimelineAssetDuration(*asset)) overrides[id] = std::move(asset);
	else {
		auto normalized = std::make_shared<TimelineAsset>(*asset);
		normalized->guid = id;
		UpdateTimelineAssetDuration(*normalized);
		overrides[id] = std::move(normalized);
	}
	if (diagnostic) diagnostic->clear();
	return true;
}

void Engine::ClearTimelineAssetOverride(ECSWorld& world, AssetID id) {

	if (auto* storage = world.GetStorage().TryGet<DefinitionStorage>()) storage->overrides.erase(id);
}

void Engine::ClearTimelineAssetOverrides(ECSWorld& world) {

	if (auto* storage = world.GetStorage().TryGet<DefinitionStorage>()) storage->overrides.clear();
}

Entity Engine::ResolveTimelineBinding(const ECSWorld& world, Entity owner,
	const TimelinePlayerSettings& settings, UUID slot) {

	const auto found = std::find_if(settings.bindings.begin(), settings.bindings.end(),
		[&](const auto& binding) { return binding.slot == slot; });
	if (found == settings.bindings.end()) return Entity::Null();
	const Entity target = found->runtimeTargetID ? world.FindByUUID(found->runtimeTargetID) : found->target.Resolve(world, owner);
	return world.IsAlive(target) && !world.IsPendingDestroy(target) ? target : Entity::Null();
}

Engine::UUID Engine::FindTimelineBinding(const TimelineAsset& asset, std::string_view name) {

	for (const auto& binding : asset.bindings) if (binding.name == name) return binding.id;
	return {};
}

Engine::UUID Engine::FindTimelineParameter(const TimelineAsset& asset, std::string_view name) {

	for (const auto& parameter : asset.parameters) if (parameter.name == name) return parameter.id;
	return {};
}

const TimelineParameterValue* Engine::GetTimelineParameterValue(const TimelineAsset& asset,
	const TimelinePlayerSettings& settings, UUID parameter) {

	for (const auto& value : settings.parameters) if (value.parameter == parameter) return &value.value;
	for (const auto& value : asset.parameters) if (value.id == parameter) return &value.value;
	return nullptr;
}

void Engine::SetTimelineParameterValue(TimelinePlayerSettings& settings, UUID parameter,
	const TimelineParameterValue& value) {

	for (auto& current : settings.parameters) {
		if (current.parameter != parameter) continue;
		current.value = value;
		return;
	}
	settings.parameters.push_back({parameter, value});
}
