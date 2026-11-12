#include "LightInfluenceSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

// c++
#include <algorithm>
#include <unordered_set>

Engine::Entity Engine::ResolveLightShadowExclusion(const ECSWorld& world, Entity owner, const LightShadowExclusion& exclusion) {

	if (exclusion.runtimeTargetID) {
		const Entity target = world.FindByUUID(exclusion.runtimeTargetID);
		if (world.IsAlive(target)) return target;
	}
	return exclusion.target.Resolve(world, owner);
}

std::vector<Engine::Entity> Engine::ResolveLightShadowExclusions(
	const ECSWorld& world, Entity owner, const LightInfluenceSettings& settings) {

	std::vector<Entity> result;
	std::unordered_set<uint64_t> visited;
	std::unordered_set<uint64_t> expanded;
	for (const auto& exclusion : settings.shadowExclusions) {

		std::vector<Entity> pending{ResolveLightShadowExclusion(world, owner, exclusion)};
		while (!pending.empty()) {

			const Entity target = pending.back();
			pending.pop_back();
			const uint64_t key = (uint64_t(target.generation) << 32u) | target.index;
			if (!world.IsAlive(target)) continue;
			if (visited.insert(key).second) result.push_back(target);
			if (!exclusion.includeChildren || !expanded.insert(key).second) continue;
			const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(target);
			Entity child = hierarchy ? hierarchy->firstChild : Entity::Null();
			while (world.IsAlive(child)) {
				pending.push_back(child);
				const auto* link = world.TryGetComponent<HierarchyComponent>(child);
				child = link ? link->nextSibling : Entity::Null();
			}
		}
	}
	return result;
}

bool Engine::IsLightShadowExcluded(const LightInfluenceSettings& settings, std::span<const Entity> exclusions,
	Entity target, uint32_t renderingLayerMask) {

	return (settings.shadowExcludedLayerMask & renderingLayerMask) != 0u ||
		std::find(exclusions.begin(), exclusions.end(), target) != exclusions.end();
}

void Engine::from_json(const nlohmann::json& in, LightShadowExclusion& exclusion) {

	exclusion.target = in.value("target", SceneEntityReference{});
	exclusion.includeChildren = in.value("includeChildren", false);
	exclusion.runtimeTargetID = {};
}

void Engine::to_json(nlohmann::json& out, const LightShadowExclusion& exclusion) {

	out = {{"target", exclusion.target}, {"includeChildren", exclusion.includeChildren}};
}

void Engine::from_json(const nlohmann::json& in, LightInfluenceSettings& settings) {

	settings.renderingLayerMask = in.value("renderingLayerMask", 0x00FFFFFFu) & 0x00FFFFFFu;
	settings.shadowExcludedLayerMask = in.value("shadowExcludedLayerMask", 0u) & 0x00FFFFFFu;
	settings.shadowExclusions = in.value("shadowExclusions", std::vector<LightShadowExclusion>{});
}

void Engine::to_json(nlohmann::json& out, const LightInfluenceSettings& settings) {

	out = {{"renderingLayerMask", settings.renderingLayerMask & 0x00FFFFFFu},
		{"shadowExcludedLayerMask", settings.shadowExcludedLayerMask & 0x00FFFFFFu},
		{"shadowExclusions", settings.shadowExclusions}};
}
