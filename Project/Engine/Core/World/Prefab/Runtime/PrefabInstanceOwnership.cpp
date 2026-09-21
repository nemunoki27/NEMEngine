#include "PrefabInstanceOwnership.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceUtility.h>

// c++

using namespace Engine::PrefabInstanceUtility;

std::vector<Engine::Entity> Engine::PrefabInstanceOwnership::CollectInstanceEntities(ECSWorld& world, UUID instanceID) {

	std::vector<Entity> entities;
	if (!instanceID) {
		return entities;
	}
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		if (world.GetComponent<PrefabLinkComponent>(entity).prefabInstanceID == instanceID) {
			entities.emplace_back(entity);
		}
		});
	return entities;
}

void Engine::PrefabInstanceOwnership::SynchronizeNestedPrefabOwnership(ECSWorld& world) {

	std::vector<Entity> roots;
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		if (world.GetComponent<PrefabLinkComponent>(entity).isPrefabRoot) {
			roots.emplace_back(entity);
		}
		});

	for (const Entity& root : roots) {

		if (!world.IsAlive(root)) {
			continue;
		}
		auto& rootLink = world.GetComponent<PrefabLinkComponent>(root);
		UUID ownerInstanceID{};

		Entity ancestor = ParentOf(world, root);
		size_t remaining = world.GetRecordCount() + 1;
		while (world.IsAlive(ancestor) && remaining-- > 0) {

			if (world.HasComponent<PrefabLinkComponent>(ancestor)) {

				const auto& ancestorLink = world.GetComponent<PrefabLinkComponent>(ancestor);
				if (ancestorLink.prefabInstanceID != rootLink.prefabInstanceID) {
					ownerInstanceID = ancestorLink.prefabInstanceID;
					break;
				}
			}
			ancestor = ParentOf(world, ancestor);
		}

		UUID nestedSlotID = rootLink.nestedSlotID;
		bool isPrefabAssetNested = rootLink.isPrefabAssetNested;
		if (!ownerInstanceID) {
			nestedSlotID = UUID{};
			isPrefabAssetNested = false;
		} else if (rootLink.ownerPrefabInstanceID != ownerInstanceID || !nestedSlotID) {
			nestedSlotID = UUID::New();
			isPrefabAssetNested = false;
		}

		for (const Entity& member : CollectInstanceEntities(world, rootLink.prefabInstanceID)) {

			auto& memberLink = world.GetComponent<PrefabLinkComponent>(member);
			memberLink.ownerPrefabInstanceID = ownerInstanceID;
			memberLink.nestedSlotID = nestedSlotID;
			memberLink.isPrefabAssetNested = isPrefabAssetNested;
		}
	}
}
