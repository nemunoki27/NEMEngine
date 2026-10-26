#include "SplineComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	// 値コピーされた参照に新しい所有枠を割り当てる
	SplineRecord& EnsureSpline(ECSWorld& world, Entity entity, SplineComponent& component) {

		auto& storage = world.GetStorage().Get<SplineStorage>();
		auto* record = storage.TryGet(component.handle);
		const auto owner = world.GetUUID(entity);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(SplineRecord{record ? record->value : SplineData{}, owner});
		return *storage.TryGet(component.handle);
	}

	// 有限な制御点だけを受け入れる
	bool IsFinite(Vector3 value) {

		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
}

void SplineComponent::InitializeStorage(ECSWorld& world, const Entity& entity, SplineComponent& component) {

	EnsureSpline(world, entity, component);
}

void SplineComponent::OnAdded(ECSWorld& world, const Entity& entity, SplineComponent& component) {

	InitializeStorage(world, entity, component);
}

void SplineComponent::ReleaseStorage(ECSWorld& world, const Entity& entity, SplineComponent& component) {

	auto& storage = world.GetStorage().Get<SplineStorage>();
	const auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(entity)) storage.Release(component.handle);
	component.handle = {};
}

void SplineComponent::DeserializeECS(ECSWorld& world, const Entity& entity, const nlohmann::json& in, SplineComponent& component) {

	SplineData candidate;
	from_json(in, candidate);
	auto& record = EnsureSpline(world, entity, component);
	record.value = std::move(candidate);
	++record.revision;
}

void SplineComponent::SerializeECS(const ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const SplineComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<SplineStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	if (!record) throw std::logic_error("Splineの保存データが見つかりません");
	to_json(out, record->value);
}

void SplineComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld,
	const Entity& entity, const SplineComponent& source, SplineComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<SplineStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<SplineStorage>().Emplace(
		SplineRecord{record ? record->value : SplineData{}, destinationWorld.GetUUID(entity), record ? record->revision : 1});
}

const SplineData* Engine::TryGetSpline(const ECSWorld& world, Entity entity) {

	const auto* component = world.TryGetComponent<SplineComponent>(entity);
	const auto* storage = world.GetStorage().TryGet<SplineStorage>();
	const auto* record = component && storage ? storage->TryGet(component->handle) : nullptr;
	return record ? &record->value : nullptr;
}

void Engine::SetSpline(ECSWorld& world, Entity entity, const SplineData& data) {

	SplineData checked;
	nlohmann::json json;
	to_json(json, data);
	from_json(json, checked);
	auto& component = world.GetComponent<SplineComponent>(entity);
	auto& record = EnsureSpline(world, entity, component);
	record.value = std::move(checked);
	++record.revision;
	world.MarkComponentModified<SplineComponent>(entity);
}

void Engine::from_json(const nlohmann::json& in, SplineData& data) {

	SplineData candidate;
	candidate.closed = in.value("closed", false);
	candidate.tolerance = in.value("tolerance", 0.002f);
	if (!std::isfinite(candidate.tolerance) || candidate.tolerance < 0.00001f || candidate.tolerance > 1)
		throw std::invalid_argument("Splineの分割精度が不正です");
	if (in.contains("points")) {
		const auto& points = in.at("points");
		if (!points.is_array() || points.size() > 4096) throw std::invalid_argument("Splineの制御点数が不正です");
		candidate.points.clear();
		for (const auto& point : points) {
			SplineControlPoint value;
			value.position = Vector3::FromJson(point.at("position"));
			value.tangentIn = Vector3::FromJson(point.at("tangentIn"));
			value.tangentOut = Vector3::FromJson(point.at("tangentOut"));
			if (!IsFinite(value.position) || !IsFinite(value.tangentIn) || !IsFinite(value.tangentOut))
				throw std::invalid_argument("Splineの制御点が不正です");
			candidate.points.push_back(value);
		}
	}
	data = std::move(candidate);
}

void Engine::to_json(nlohmann::json& out, const SplineData& data) {

	out = {{"closed", data.closed}, {"tolerance", data.tolerance}, {"points", nlohmann::json::array()}};
	for (const auto& point : data.points) out["points"].push_back({{"position", point.position.ToJson()},
		{"tangentIn", point.tangentIn.ToJson()}, {"tangentOut", point.tangentOut.ToJson()}});
}
