#include "TimelineControl.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <string>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace Engine;

namespace {

	struct ControlClaim {

		std::string component, property;
		TimelineControlOwner owner;
	};
	struct ControlStorage {

		std::unordered_map<uint64_t, std::vector<ControlClaim>> targets;
	};

	// 再利用されたEntityを別の制御先として扱う
	uint64_t EntityKey(Entity entity, std::string_view component) {

		if (component == "GlobalTime") return (std::numeric_limits<uint64_t>::max)();
		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	// Component全体と個別プロパティの制御範囲を比較する
	bool Matches(const ControlClaim& claim, std::string_view component, std::string_view property) {

		if (claim.component != component) return false;
		auto canonical = [&](std::string_view value) {
			if (component == "Transform") {
				if (value == "localPos2D") return std::string_view{"localPos"};
				if (value == "localRotationZ") return std::string_view{"localRotation"};
				if (value == "localScale2D") return std::string_view{"localScale"};
			}
			return value;
		};
		const auto first = canonical(claim.property), second = canonical(property);
		if (first.empty() || second.empty() || first == second) return true;
		const auto child = [](std::string_view parent, std::string_view value) {
			return value.size() > parent.size() && value.starts_with(parent) &&
				(value[parent.size()] == '.' || value[parent.size()] == '[');
		};
		return child(first, second) || child(second, first);
	}

	// 同じPlayer内のブレンドを維持して制御するPlayerを選ぶ
	const TimelineControlOwner* Winner(const ControlStorage* storage, Entity entity,
		std::string_view component, std::string_view property) {

		if (!storage) return nullptr;
		const auto found = storage->targets.find(EntityKey(entity, component));
		if (found == storage->targets.end()) return nullptr;
		const TimelineControlOwner* winner = nullptr;
		for (const auto& claim : found->second) {
			if (!Matches(claim, component, property)) continue;
			if (!winner || claim.owner.priority > winner->priority ||
				(claim.owner.priority == winner->priority && claim.owner.order > winner->order)) winner = &claim.owner;
		}
		return winner;
	}
}

bool Engine::IsTimelineControlled(const ECSWorld& world, Entity entity,
	std::string_view component, std::string_view property) {

	return Winner(world.GetStorage().TryGet<ControlStorage>(), entity, component, property) != nullptr;
}

bool Engine::AcquireTimelineControl(ECSWorld& world, Entity entity, std::string_view component,
	std::string_view property, const TimelineControlOwner& owner) {

	if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) return false;
	auto& claims = world.GetStorage().Get<ControlStorage>().targets[EntityKey(entity, component)];
	const auto found = std::find_if(claims.begin(), claims.end(), [&](const auto& claim) {
		return claim.owner.player == owner.player && claim.owner.instance == owner.instance &&
			claim.component == component && claim.property == property;
	});
	if (found == claims.end()) claims.push_back({std::string(component), std::string(property), owner});
	return OwnsTimelineControl(world, entity, component, property, owner);
}

bool Engine::OwnsTimelineControl(const ECSWorld& world, Entity entity, std::string_view component,
	std::string_view property, const TimelineControlOwner& owner) {

	const auto* winner = Winner(world.GetStorage().TryGet<ControlStorage>(), entity, component, property);
	return winner && winner->player == owner.player;
}

bool Engine::CanRestoreTimelineControl(const ECSWorld& world, Entity entity, std::string_view component,
	std::string_view property, const TimelineControlOwner& owner) {

	const auto* storage = world.GetStorage().TryGet<ControlStorage>();
	if (!storage) return true;
	const auto found = storage->targets.find(EntityKey(entity, component));
	if (found == storage->targets.end()) return true;
	for (const auto& claim : found->second) {
		if (Matches(claim, component, property) &&
			(claim.owner.player != owner.player || claim.owner.instance != owner.instance)) return false;
	}
	return true;
}

void Engine::ReleaseTimelineControls(ECSWorld& world, const TimelineControlOwner& owner) {

	if (!world.GetStorage().TryGet<ControlStorage>()) return;
	auto& targets = world.GetStorage().Get<ControlStorage>().targets;
	for (auto it = targets.begin(); it != targets.end();) {
		std::erase_if(it->second, [&](const auto& claim) {
			return claim.owner.player == owner.player && claim.owner.instance == owner.instance;
		});
		if (it->second.empty()) it = targets.erase(it);
		else ++it;
	}
}

void Engine::ReleaseTimelineControl(ECSWorld& world, Entity entity, std::string_view component,
	std::string_view property, const TimelineControlOwner& owner) {

	auto* storage = world.GetStorage().TryGet<ControlStorage>();
	if (!storage) return;
	const auto found = storage->targets.find(EntityKey(entity, component));
	if (found == storage->targets.end()) return;
	std::erase_if(found->second, [&](const auto& claim) {
		return claim.owner.player == owner.player && claim.owner.instance == owner.instance &&
			claim.component == component && claim.property == property;
	});
	if (found->second.empty()) storage->targets.erase(found);
}

void Engine::ReleaseTimelinePlayerControls(ECSWorld& world, UUID player) {

	if (!world.GetStorage().TryGet<ControlStorage>()) return;
	auto& targets = world.GetStorage().Get<ControlStorage>().targets;
	for (auto it = targets.begin(); it != targets.end();) {
		std::erase_if(it->second, [&](const auto& claim) { return claim.owner.player == player; });
		if (it->second.empty()) it = targets.erase(it);
		else ++it;
	}
}
